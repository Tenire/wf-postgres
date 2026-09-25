#include <iostream>
#include <string>
#include <arpa/inet.h>
#include "workflow/URIParser.h"
#include "workflow/StringUtil.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/WFFacilities.h"
#include <atomic>
#include "WFPostgresClient.h"

using namespace wfpg;
using namespace wfpg::protocol;

class MockStartupRequest : public PostgresRequest {
public:
    std::string user_ = "postgres";
    std::string db_;
    std::string forced_option_key;
    std::string forced_option_val;
    uint32_t proto_version = 196610; // 3.2

    int encode(struct iovec vectors[], int max) override {
        std::string packet;
        std::string params;
        
        params += "user"; params.push_back('\0');
        params += user_; params.push_back('\0');
        
        if (!db_.empty()) {
            params += "database"; params.push_back('\0');
            params += db_; params.push_back('\0');
        }
        
        if (!forced_option_key.empty()) {
            params += forced_option_key; params.push_back('\0');
            params += forced_option_val; params.push_back('\0');
        }
        params.push_back('\0'); // Final terminator

        uint32_t len = htonl(4 + 4 + params.size());
        uint32_t proto = htonl(proto_version);
        packet.append((char*)&len, 4);
        packet.append((char*)&proto, 4);
        packet.append(params);
        
        buf_ = packet;
        vectors[0].iov_base = (void *)buf_.c_str();
        vectors[0].iov_len = buf_.size();
        return 1;
    }
private:
    std::string buf_;
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <postgres_url>" << std::endl;
        return 1;
    }
    
    ParsedURI uri;
    if (URIParser::parse(argv[1], uri) != 0) {
        std::cerr << "Invalid URL" << std::endl;
        return 1;
    }
    
    std::string host = uri.host ? uri.host : "127.0.0.1";
    int port = uri.port ? atoi(uri.port) : 5432;
    bool is_ssl = (uri.scheme && (strcasecmp(uri.scheme, "postgresqls") == 0 || strcasecmp(uri.scheme, "tcps") == 0));
    std::string scheme = is_ssl ? "tcps://" : "tcp://";
    std::string target_url = scheme + host + ":" + std::to_string(port);

    std::string user = "postgres";
    std::string pass;
    std::string db;
    if (uri.userinfo) {
        const char *colon = strchr(uri.userinfo, ':');
        if (colon) {
            user.assign(uri.userinfo, colon - uri.userinfo);
            pass.assign(colon + 1);
            StringUtil::url_decode(pass);
        } else {
            user.assign(uri.userinfo);
        }
        StringUtil::url_decode(user);
    }
    if (uri.path && uri.path[0] == '/' && uri.path[1]) {
        db.assign(uri.path + 1);
        StringUtil::url_decode(db);
    }

    WFFacilities::WaitGroup wait_group(1);
    std::atomic<int> test_result{0};

    std::cout << "Starting plain_32 test..." << std::endl;
    auto *task1 = WFNetworkTaskFactory<MockStartupRequest, PostgresResponse>::create_client_task(
        TT_TCP, target_url, 0, [&wait_group, target_url, user, pass, db, &test_result](WFNetworkTask<MockStartupRequest, PostgresResponse> *task) {
            if (task->get_state() != WFT_STATE_SUCCESS) {
                std::cerr << "plain_32 network error: " << task->get_error() << std::endl;
                test_result = 1;
            } else {
                auto *resp = task->get_resp();
                std::cout << "plain_32 test completed." << std::endl;
                if (resp->get_negotiated_protocol_version() != 0) {
                    std::cout << "plain_32 Negotiated Version: " << resp->get_negotiated_protocol_version() << std::endl;
                } else if (resp->is_error()) {
                    std::cout << "plain_32 Error: " << resp->get_error().message << std::endl;
                    test_result = 1;
                } else {
                    std::cout << "plain_32 succeeded directly (No 'v' frame, natural auth)." << std::endl;
                }
            }
            
            std::cout << "\nStarting forced_option test..." << std::endl;
            auto *task2 = WFNetworkTaskFactory<MockStartupRequest, PostgresResponse>::create_client_task(
                TT_TCP, target_url, 0, [&wait_group, &test_result](WFNetworkTask<MockStartupRequest, PostgresResponse> *t) {
                    if (t->get_state() != WFT_STATE_SUCCESS) {
                        std::cerr << "forced_option network error: " << t->get_error() << std::endl;
                        test_result = 1;
                    } else {
                        auto *r = t->get_resp();
                        std::cout << "forced_option test completed." << std::endl;
                        if (r->get_negotiated_protocol_version() != 0) {
                            std::cout << "forced_option Negotiated Version: " << r->get_negotiated_protocol_version() << std::endl;
                            bool found = false;
                            for (const auto& opt : r->get_negotiated_unsupported_options()) {
                                std::cout << "Unsupported Option Received: " << opt << std::endl;
                                if (opt == "_pq_.unsupported_test") found = true;
                            }
                            if (!found) {
                                std::cerr << "Error: server sent 'v' frame but _pq_.unsupported_test was NOT in the unsupported options list!" << std::endl;
                                test_result = 1;
                            }
                        } else {
                            std::cerr << "Error: server did not send 'v' frame for unsupported option!" << std::endl;
                            test_result = 1;
                        }
                    }
                    wait_group.done();
                });
            
            task2->get_req()->user_ = user;
            task2->get_req()->db_ = db;
            task2->get_req()->forced_option_key = "_pq_.unsupported_test";
            task2->get_req()->forced_option_val = "1";
            task2->get_resp()->set_is_startup(true);
            task2->get_resp()->set_auth(user, pass);
            task2->start();
        });
    
    task1->get_req()->user_ = user;
    task1->get_req()->db_ = db;
    task1->get_resp()->set_is_startup(true);
    task1->get_resp()->set_auth(user, pass);
    task1->start();
    
    wait_group.wait();
    
    if (test_result == 0) {
        std::cout << "\nAll protocol negotiation tests passed!" << std::endl;
    } else {
        std::cerr << "\nProtocol negotiation tests FAILED!" << std::endl;
    }
    
    return test_result.load();
}
