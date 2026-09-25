#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include "workflow/URIParser.h"
#include <openssl/x509v3.h>
#include "WFPostgresConnection.h"
#include "PostgresSSLMessage.h"
#include "workflow/StringUtil.h"
#include "WFPostgresError.h"
#include <algorithm>
#include <map>

namespace wfpg {

namespace {

static void parse_query(const char* query, std::map<std::string, std::string>& query_map) {
    if (!query) return;
    auto orig_map = URIParser::split_query(query);
    for (auto& kv : orig_map) {
        std::string key = kv.first;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        StringUtil::url_decode(kv.second);
        query_map[key] = kv.second;
    }
}

static bool is_ssl_required(const ParsedURI& uri, const std::map<std::string, std::string>& query_map) {
    if (uri.scheme && strcasecmp(uri.scheme, "postgresqls") == 0) {
        return true;
    }
    auto it = query_map.find("sslmode");
    if (it != query_map.end() && (it->second == "require" || it->second == "verify-ca" || it->second == "verify-full")) {
        return true;
    }
    return false;
}

static bool setup_ssl_ctx(SSL_CTX*& ssl_ctx, bool& owns_ssl_ctx, const ParsedURI& uri, const std::map<std::string, std::string>& query_map, bool default_verify_none, int& state, int& error) {
    auto sslmode_it = query_map.find("sslmode");
    bool verify_peer = (sslmode_it != query_map.end() && (sslmode_it->second == "verify-full" || sslmode_it->second == "verify-ca"));
    
    if (verify_peer || default_verify_none) {
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            state = WFT_STATE_TASK_ERROR;
            error = WFT_ERR_POSTGRES_SSL_INIT_FAILED;
            return false;
        }
        owns_ssl_ctx = true;

        if (verify_peer) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
            if (sslmode_it->second == "verify-full" && uri.host) {
                X509_VERIFY_PARAM *param = SSL_CTX_get0_param(ssl_ctx);
                X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            }
            auto sslrootcert_it = query_map.find("sslrootcert");
            if (sslrootcert_it != query_map.end()) {
                if (SSL_CTX_load_verify_locations(ssl_ctx, sslrootcert_it->second.c_str(), NULL) != 1) {
                    SSL_CTX_free(ssl_ctx);
                    ssl_ctx = nullptr;
                    owns_ssl_ctx = false;
                    state = WFT_STATE_TASK_ERROR;
                    error = WFT_ERR_POSTGRES_SSL_CERT_FAILED;
                    return false;
                }
            }
        } else if (default_verify_none) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        }
    }
    return true;
}

} // namespace

int WFPostgresConnection::init(const std::string& url, SSL_CTX *ssl_ctx)
{
    std::string query;
    ParsedURI uri;

    if (URIParser::parse(url, uri) >= 0)
    {
        if (uri.query)
        {
            query = uri.query;
            query += '&';
        }

        std::map<std::string, std::string> query_map;
        parse_query(uri.query, query_map);
        bool is_ssl = is_ssl_required(uri, query_map);

        if (ssl_ctx) {
            this->ssl_ctx = ssl_ctx;
            this->owns_ssl_ctx = false;
        } else {
            if (is_ssl) {
                int state = 0, error = 0;
                if (!setup_ssl_ctx(this->ssl_ctx, this->owns_ssl_ctx, uri, query_map, true, state, error)) {
                    errno = EINVAL;
                    return -1;
                }
            }
        }

        query += "transaction=INTERNAL_CONN_ID_" + std::to_string(this->id);
        free(uri.query);
        uri.query = strdup(query.c_str());
        if (uri.query)
        {
            this->uri = std::move(uri);
            return 0;
        }
    }
    else if (uri.state == URI_STATE_INVALID)
        errno = EINVAL;

    return -1;
}

WFPostgresTask *WFPostgresConnection::create_query_task(const std::string& query,
                                            postgres_callback_t callback)
{
    auto cb = [this, callback](WFPostgresTask *task) {
        if (task && task->get_resp()) {
            this->last_tx_state_ = task->get_resp()->get_transaction_state();
            if (task->get_resp()->get_backend_pid() != 0) {
                this->backend_pid_ = task->get_resp()->get_backend_pid();
                this->backend_secret_data_ = task->get_resp()->get_backend_secret_data();
            }
        }
        if (callback) {
            callback(task);
        }
    };
    WFPostgresTask *task = WFPostgresTaskFactory::create_postgres_task(this->uri, 0, std::move(cb));
    this->set_ssl_ctx(task);
    task->get_req()->set_query(query);
    return task;
}

WFPostgresTask *WFPostgresConnection::create_query_task(const std::string& query,
                                            const std::vector<protocol::PostgresParameter>& params,
                                            postgres_callback_t callback)
{
    auto cb = [this, callback](WFPostgresTask *task) {
        if (task && task->get_resp()) {
            this->last_tx_state_ = task->get_resp()->get_transaction_state();
            if (task->get_resp()->get_backend_pid() != 0) {
                this->backend_pid_ = task->get_resp()->get_backend_pid();
                this->backend_secret_data_ = task->get_resp()->get_backend_secret_data();
            }
        }
        if (callback) {
            callback(task);
        }
    };
    WFPostgresTask *task = WFPostgresTaskFactory::create_postgres_task(this->uri, 0, std::move(cb));
    this->set_ssl_ctx(task);
    task->get_req()->set_query(query, params);
    return task;
}

WFPostgresTask *WFPostgresConnection::create_disconnect_task(postgres_callback_t callback)
{
    WFPostgresTask *task =
        WFPostgresTaskFactory::create_disconnect_task(this->uri, 0, std::move(callback));
    this->set_ssl_ctx(task);
    return task;
}

WFPostgresTask *WFPostgresConnection::create_cancel_task(postgres_callback_t callback)
{
    WFPostgresTask *task = WFPostgresTaskFactory::create_cancel_task(
        this->uri, this->backend_pid_, this->backend_secret_data_, 0, std::move(callback));
    this->set_ssl_ctx(task);
    return task;
}

void WFPostgresConnection::set_ssl_ctx(WFPostgresTask *task) const
{
    protocol::__postgres_task_set_ssl_ctx(task, this->ssl_ctx);
}

} // namespace wfpg
