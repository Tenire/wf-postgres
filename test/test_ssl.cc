#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"
#include <iostream>
#include <atomic>
#include <vector>
#include <openssl/x509v3.h>

using namespace wfpg;
using namespace wfpg::protocol;

int main(int argc, char *argv[])
{
    // Default to postgresqls:// to enforce SSL handshake
    std::string url = "postgresqls://wf_tester:wf_secret@localhost:5434/wf_testdb";
    if (argc > 1) url = argv[1];

    WFFacilities::WaitGroup wait_group(1);
    ParsedURI uri;
    URIParser::parse(url, uri);

    bool is_direct = (argc > 2 && std::string(argv[2]) == "direct");
    WFPostgresConnection *conn = nullptr;
    WFPostgresTask *task = nullptr;

    std::atomic<int> exit_code{0};

    auto callback = [&wait_group, &exit_code](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "SSL Connection failed: state=" << task->get_state() << " error=" << task->get_error() << "\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        if (task->get_resp()->is_error()) {
            std::cerr << "Query error: " << task->get_resp()->get_error().message << "\n";
            exit_code = 1;
        } else {
            std::cout << "SSL Connection successful! Verified by server response.\n";
            PostgresResultCursor cursor(task->get_resp());
            std::vector<PostgresCell> row;
            if (cursor.fetch_row(row) && !row.empty()) {
                if (row[0].as_string() == "t") {
                    std::cout << "Server confirmed SSL is active.\n";
                } else {
                    std::cerr << "Server reported SSL is NOT active!\n";
                    exit_code = 1;
                }
            } else {
                std::cerr << "Server did not return a row!\n";
                exit_code = 1;
            }
        }
        wait_group.done();
    };

    if (is_direct) {
        task = WFPostgresTaskFactory::create_postgres_task(url, 0, callback);
        task->get_req()->set_query("SELECT ssl FROM pg_stat_ssl WHERE pid = pg_backend_pid();");
    } else {
        conn = new WFPostgresConnection(1);
        int ret = conn->init(url);
        if (ret != 0) {
            std::cerr << "Connection init failed\n";
            return 1;
        }
        task = conn->create_query_task("SELECT ssl FROM pg_stat_ssl WHERE pid = pg_backend_pid();", callback);
    }

    task->start();
    wait_group.wait();
    if (conn) {
        conn->deinit();
        delete conn;
    }
    
    return exit_code.load();
}
