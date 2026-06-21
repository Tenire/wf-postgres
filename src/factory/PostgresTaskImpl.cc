#include "PostgresTask.h"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <errno.h>
#include "workflow/RouteManager.h"
#include "PostgresInternal.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/URIParser.h"
#include "workflow/StringUtil.h"
#include "workflow/WFTask.h"
#include "WFPostgresError.h"
#include "workflow/WFConnection.h"
#include "workflow/SSLWrapper.h"
#include "workflow/WFGlobal.h"
#include <algorithm>
#include <map>

#define POSTGRES_KEEPALIVE_DEFAULT     (30 * 1000)
#define POSTGRES_KEEPALIVE_TRANSACTION (3600 * 1000)

namespace wfpg {
namespace protocol {
namespace {

enum PostgresProtocolState {
    PG_PROTOCOL_READY = 0,
    PG_PROTOCOL_COPY_IN = 1,
    PG_PROTOCOL_NOTIFY_WAIT = 2,
    PG_PROTOCOL_BROKEN = 3
};

struct PostgresConnection : public WFConnection
{
    SSL *ssl;
    int state;
    PostgresProtocolState protocol_state;
    ::protocol::SSLWrapper *wrapper;
    int32_t backend_pid;
    int32_t backend_secret;
    std::string backend_secret_data;
    SSL_CTX *ssl_ctx;

    PostgresConnection(SSL *s) : 
        ssl(s), state(0), protocol_state(PG_PROTOCOL_READY), 
        wrapper(nullptr), backend_pid(0), backend_secret(0), ssl_ctx(NULL)
    {
    }
    virtual ~PostgresConnection()
    {
        if (wrapper) delete wrapper;
        if (ssl) SSL_free(ssl);
    }
};

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

static void extract_auth_info(const ParsedURI& uri, std::string& user, std::string& pass, std::string& db) {
    if (uri.userinfo) {
        const char *colon = strchr(uri.userinfo, ':');
        if (colon) {
            user.assign(uri.userinfo, colon - uri.userinfo);
            pass.assign(colon + 1);
        } else {
            user.assign(uri.userinfo);
        }
    }

    if (uri.path && uri.path[0] == '/' && uri.path[1]) {
        db.assign(uri.path + 1);
    }

    StringUtil::url_decode(user);
    StringUtil::url_decode(pass);
    StringUtil::url_decode(db);
}

static void extract_startup_params(
    const std::map<std::string, std::string>& query_map,
    std::map<std::string, std::string>& startup_params)
{
    startup_params["client_encoding"] = "UTF8";
    
    for (const auto& kv : query_map) {
        if (kv.first == "client_encoding") {
            startup_params["client_encoding"] = kv.second;
        } else if (kv.first == "application_name" || kv.first == "options") {
            startup_params[kv.first] = kv.second;
        } else if (kv.first == "timezone") {
            startup_params["TimeZone"] = kv.second;
        } else if (kv.first.size() > 8 && kv.first.compare(0, 8, "pgparam.") == 0) {
            startup_params[kv.first.substr(8)] = kv.second;
        }
    }
}

static SSL* create_ssl(SSL_CTX *ssl_ctx)
{
    BIO *wbio;
    BIO *rbio;
    SSL *ssl;

    rbio = BIO_new(BIO_s_mem());
    if (rbio)
    {
        wbio = BIO_new(BIO_s_mem());
        if (wbio)
        {
            ssl = SSL_new(ssl_ctx);
            if (ssl)
            {
                SSL_set_bio(ssl, rbio, wbio);
                return ssl;
            }
            BIO_free(wbio);
        }
        BIO_free(rbio);
    }
    return NULL;
}

static bool handle_starttls_transition(
    SSL_CTX* task_ssl_ctx,
    PostgresSSLResponse* ssl_resp, 
    bool verify_full, 
    const ParsedURI& uri, 
    PostgresConnection*& conn, 
    int& state, 
    int& error, 
    WFConnection* wf_conn) 
{
    if (ssl_resp->get_byte() == 'S') {
        SSL_CTX *ctx = task_ssl_ctx;
        if (!ctx) ctx = WFGlobal::get_ssl_client_ctx();
        SSL *ssl = create_ssl(ctx);
        if (!ssl) {
            state = WFT_STATE_TASK_ERROR;
            error = WFT_ERR_POSTGRES_SSL_INIT_FAILED;
            return true;
        }
        if (verify_full && uri.host) {
            X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
            X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            X509_VERIFY_PARAM_set1_host(param, uri.host, 0);
        }
        SSL_set_connect_state(ssl);

        auto *my_conn = new PostgresConnection(ssl);
        my_conn->state = 1; // Handshake next
        wf_conn->set_context(my_conn, [](void *ctx) {
            delete (PostgresConnection *)ctx;
        });
        conn = my_conn;
        return false; // Continue with seq=1
    } else {
        state = WFT_STATE_TASK_ERROR;
        error = WFT_ERR_POSTGRES_SSL_NOT_SUPPORTED;
        return true; // Abort
    }
}

static ::protocol::ProtocolMessage* wrap_ssl(PostgresConnection* conn, ::protocol::ProtocolMessage* msg) {
    if (!conn->wrapper) conn->wrapper = new ::protocol::SSLWrapper(msg, conn->ssl);
    return (::protocol::ProtocolMessage*)conn->wrapper;
}

static bool handle_startup_auth_completion(
    PostgresResponse* startup_resp, 
    PostgresConnection*& conn, 
    bool& is_user_request, 
    int& state, 
    int& error, 
    ::WFConnection* wf_conn) 
{
    if (startup_resp->is_error()) {
        state = WFT_STATE_TASK_ERROR;
        int internal_err = PostgresInternalAccess::get_internal_error(startup_resp);
        if (internal_err != 0) {
            error = internal_err;
        } else {
            const std::string& sql_state = startup_resp->get_sql_state();
            if (sql_state == "28P01" || sql_state == "28000") {
                error = WFT_ERR_POSTGRES_AUTH_FAILED;
            } else {
                error = WFT_ERR_POSTGRES_PROTOCOL_ERROR;
            }
        }
        return true;
    }
    if (PostgresInternalAccess::get_negotiated_protocol_version(startup_resp) != 0 &&
        (PostgresInternalAccess::get_negotiated_protocol_version(startup_resp) >> 16) != 3) {
        state = WFT_STATE_TASK_ERROR;
        error = WFT_ERR_POSTGRES_PROTOCOL_NOT_SUPPORTED; 
        return true;
    }
    if (!conn) {
        auto *my_conn = new PostgresConnection(nullptr);
        wf_conn->set_context(my_conn, [](void *ctx) {
            delete (PostgresConnection *)ctx;
        });
        conn = my_conn;
    }
    conn->backend_pid = PostgresInternalAccess::get_backend_pid(startup_resp);
    conn->backend_secret = PostgresInternalAccess::get_backend_secret_key(startup_resp);
    conn->backend_secret_data = PostgresInternalAccess::get_backend_secret_data(startup_resp);
    conn->state = 3; // Ready
    is_user_request = true;
    return false;
}

static bool check_reconnect_guard(
    bool is_fixed_conn, 
    ::RouteManager::RouteTarget* target, 
    PostgresConnection* conn, 
    int& error_code) 
{
    if (conn && conn->state == 3) {
        if (is_fixed_conn && target) {
            if (target->state) {
                error_code = ECONNRESET;
                return false;
            }
            target->state = 1;
        }
        conn->state = 4;
    }
    return true;
}

static void cleanup_fixed_conn_target(
    bool is_fixed_conn, 
    ::RouteManager::RouteTarget* target, 
    int task_state, 
    int keep_alive_timeo) 
{
    if (is_fixed_conn) {
        if (task_state != WFT_STATE_SUCCESS || keep_alive_timeo == 0) {
            if (target) {
                target->state = 0;
            }
        }
    }
}

static bool validate_request(PostgresConnection* conn, PostgresRequest* req, int& error_code) {
    if (!conn) return true;

    bool is_copy_task = PostgresInternalAccess::is_copy(req) || PostgresInternalAccess::is_copy_done(req) || PostgresInternalAccess::is_copy_fail(req);
    bool is_disconnect = PostgresInternalAccess::is_disconnect(req);

    if (conn->protocol_state == PG_PROTOCOL_BROKEN) {
        error_code = EBADF;
        return false;
    } else if (conn->protocol_state == PG_PROTOCOL_COPY_IN) {
        if (!is_copy_task && !is_disconnect) {
            error_code = EPROTO;
            return false;
        }
    } else if (conn->protocol_state == PG_PROTOCOL_NOTIFY_WAIT) {
        if (!is_disconnect) {
            error_code = EPROTO;
            return false;
        }
    } else if (conn->protocol_state == PG_PROTOCOL_READY) {
        if (is_copy_task) {
            error_code = EPROTO;
            return false;
        }
        if (PostgresInternalAccess::is_wait_notification(req)) {
            conn->protocol_state = PG_PROTOCOL_NOTIFY_WAIT;
        }
    }
    return true;
}

static void update_after_response(PostgresConnection* conn, PostgresRequest* req, PostgresResponse* resp, int task_state) {
    if (!conn) return;

    if (resp) {
        PostgresInternalAccess::set_backend_pid(resp, conn->backend_pid);
        PostgresInternalAccess::set_backend_secret_key(resp, conn->backend_secret);
        PostgresInternalAccess::set_backend_secret_data(resp, conn->backend_secret_data);

        // If it's a network error, or a protocol error (not just a SQL error response), break the connection
        if (task_state != WFT_STATE_SUCCESS && !(task_state == WFT_STATE_TASK_ERROR && resp->is_error())) {
            conn->protocol_state = PG_PROTOCOL_BROKEN;
        } else {
            if (PostgresInternalAccess::is_copy_in(resp)) {
                conn->protocol_state = PG_PROTOCOL_COPY_IN;
            } else if (req && PostgresInternalAccess::is_wait_notification(req)) {
                conn->protocol_state = PG_PROTOCOL_READY;
            } else if (resp->get_transaction_state() == 'I' ||
                       resp->get_transaction_state() == 'T' ||
                       resp->get_transaction_state() == 'E') {
                conn->protocol_state = PG_PROTOCOL_READY;
            }
        }
    } else if (task_state != WFT_STATE_SUCCESS) {
        conn->protocol_state = PG_PROTOCOL_BROKEN;
    }
}

} // anonymous namespace

class ComplexPostgresTask : public ::WFComplexClientTask<PostgresRequest, PostgresResponse>
{
public:
    ComplexPostgresTask(int retry_max, postgres_callback_t&& callback)
        : WFComplexClientTask(retry_max, std::move(callback)) {}
    virtual ~ComplexPostgresTask() {
        if (owns_ssl_ctx_ && my_ssl_ctx_) {
            SSL_CTX_free(my_ssl_ctx_);
        }
        delete startup_req_;
        delete ssl_req_;
        delete ssl_handshaker_;
        delete startup_resp_;
        delete ssl_resp_;
    }

protected:
    virtual bool check_request() override;
    virtual ::CommMessageOut *message_out() override;
    virtual ::CommMessageIn *message_in() override;
    virtual int keep_alive_timeout() override;
    virtual int first_timeout() override;
    virtual bool init_success() override;
    virtual bool finish_once() override;
    virtual void handle(int state, int error) override;
    virtual ::WFConnection *get_connection() const override;
public:
    void set_my_ssl_ctx(SSL_CTX *ctx) {
        if (my_ssl_ctx_ == ctx) return;
        if (owns_ssl_ctx_ && my_ssl_ctx_) {
            SSL_CTX_free(my_ssl_ctx_);
        }
        my_ssl_ctx_ = ctx;
        owns_ssl_ctx_ = false;
    }
    SSL_CTX *get_my_ssl_ctx() const { return my_ssl_ctx_; }

private:
    std::string user_;
    std::string pass_;
    std::string db_;
    std::map<std::string, std::string> startup_params_;
    bool is_user_request_ = true;
    bool is_ssl_ = false;
    bool verify_full_ = false;
    bool owns_ssl_ctx_ = false;
    SSL_CTX *my_ssl_ctx_{nullptr};
    PostgresRequest *startup_req_{nullptr};
    PostgresSSLRequest *ssl_req_{nullptr};
    ::protocol::SSLHandshaker *ssl_handshaker_{nullptr};
    PostgresResponse *startup_resp_{nullptr};
    PostgresSSLResponse *ssl_resp_{nullptr};
    uint32_t target_protocol_version_ = 196610; // Default 3.2
};

bool ComplexPostgresTask::check_request()
{
    return true;
}

CommMessageOut *ComplexPostgresTask::message_out()
{
    PostgresRequest *req;
    is_user_request_ = false;
    auto *wf_conn = this->WFComplexClientTask::get_connection();
    auto *conn = (PostgresConnection *)(wf_conn ? wf_conn->get_context() : nullptr);

    // ==========================================
    // 1. Connection State (Startup & SSL)
    // ==========================================
    if (this->get_seq() == 0 && !this->get_req()->is_cancel() && (!conn || (is_ssl_ && conn->state < 3))) {
        if (is_ssl_) {
            if (!ssl_req_) ssl_req_ = new PostgresSSLRequest();
            return ssl_req_;
        } else {
            if (!startup_req_) {
                startup_req_ = new PostgresRequest();
                startup_req_->set_is_startup(true);
                startup_req_->set_protocol_version(target_protocol_version_);
                startup_req_->set_auth(user_, db_, pass_);
                PostgresInternalAccess::set_startup_params(startup_req_, startup_params_);
            }
            return startup_req_;
        }
    }

    if (is_ssl_ && conn && conn->state == 1) { // Handshake
        if (!ssl_handshaker_) ssl_handshaker_ = new ::protocol::SSLHandshaker(conn->ssl);
        return ssl_handshaker_;
    } else if (is_ssl_ && conn && conn->state == 2) { // SSL Startup
        if (!startup_req_) {
            startup_req_ = new PostgresRequest();
            startup_req_->set_is_startup(true);
            startup_req_->set_protocol_version(target_protocol_version_);
            startup_req_->set_auth(user_, db_, pass_);
            PostgresInternalAccess::set_startup_params(startup_req_, startup_params_);
        }
        return wrap_ssl(conn, startup_req_);
    }

    // ==========================================
    // 2. User Request Validation (Protocol State)
    // ==========================================
    req = this->get_req();
    int error_code = 0;
    if (!validate_request(conn, req, error_code)) {
        errno = error_code;
        return nullptr;
    }

    // ==========================================
    // 3. User Request Preparation
    // ==========================================
    is_user_request_ = true;
    if (!check_reconnect_guard(this->is_fixed_conn(), (RouteManager::RouteTarget *)this->target, conn, error_code)) {
        errno = error_code;
        return nullptr;
    }
    
    if (conn && conn->ssl) {
        return wrap_ssl(conn, req);
    }
    return req;
}

CommMessageIn *ComplexPostgresTask::message_in()
{
    auto *wf_conn = this->WFComplexClientTask::get_connection();
    auto *conn = (PostgresConnection *)(wf_conn ? wf_conn->get_context() : nullptr);

    if (!is_user_request_) {
        if (is_ssl_ && (!conn || conn->state == 0)) {
            if (!ssl_resp_) ssl_resp_ = new PostgresSSLResponse();
            return ssl_resp_;
        } else if (conn && conn->state == 1) {
            return ssl_handshaker_;
        } else {
            if (!startup_resp_) {
                startup_resp_ = new PostgresResponse();
                startup_resp_->clear_buffer();
                startup_resp_->set_auth(user_, pass_);
                startup_resp_->set_is_startup(true);
            }
            if (conn && conn->ssl) {
                return (CommMessageIn*)wrap_ssl(conn, startup_resp_);
            }
            return startup_resp_;
        }
    }

    PostgresResponse *resp = this->get_resp();
    resp->clear_buffer();
    resp->set_auth(user_, pass_);
    resp->set_is_startup(false);
    
    if (is_ssl_ && conn && conn->ssl) {
        return (CommMessageIn*)wrap_ssl(conn, resp);
    }
    return resp;
}

int ComplexPostgresTask::keep_alive_timeout()
{
    if (this->state != WFT_STATE_SUCCESS)
        return 0;

    if (!is_user_request_) {
        return POSTGRES_KEEPALIVE_DEFAULT; // Keep connection alive during authentication
    }

    if (is_user_request_ && (this->get_req()->is_disconnect() || this->get_req()->is_cancel())) {
        return 0;
    }

    auto *resp = (protocol::PostgresResponse *)this->get_resp();
    char tx_state = resp->get_transaction_state();

    if (tx_state == 'T' || tx_state == 'E') {
        if (!this->is_fixed_conn()) {
            return 0;
        }
    }

    return this->keep_alive_timeo;
}

int ComplexPostgresTask::first_timeout()
{
    return this->watch_timeo;
}

bool ComplexPostgresTask::init_success()
{
    std::map<std::string, std::string> query_map;
    
    if (!uri_.scheme || (strcasecmp(uri_.scheme, "postgres") != 0 && strcasecmp(uri_.scheme, "postgresql") != 0 && strcasecmp(uri_.scheme, "postgresqls") != 0)) {
        this->state = WFT_STATE_TASK_ERROR;
        this->error = WFT_ERR_URI_SCHEME_INVALID;
        return false;
    }

    parse_query(this->uri_.query, query_map);
    is_ssl_ = is_ssl_required(this->uri_, query_map);
    
    auto sslmode_it = query_map.find("sslmode");
    if (sslmode_it != query_map.end() && sslmode_it->second == "verify-full" && uri_.host) {
        verify_full_ = true;
    }

    extract_auth_info(this->uri_, user_, pass_, db_);
    extract_startup_params(query_map, startup_params_);

    if (is_ssl_ && !my_ssl_ctx_) {
        if (!setup_ssl_ctx(my_ssl_ctx_, owns_ssl_ctx_, this->uri_, query_map, false, this->state, this->error)) {
            return false;
        }
    }

    std::string info = std::string(is_ssl_ ? "postgresqls" : "postgres") + "|user:" + user_ + "|pass:" + pass_ + "|db:" + db_;
    if (uri_.host) info += "|host:" + std::string(uri_.host);
    if (uri_.port) info += "|port:" + std::string(uri_.port);

    if (query_map.count("application_name")) info += "|application_name:" + query_map["application_name"];
    if (query_map.count("options")) info += "|options:" + query_map["options"];
    if (query_map.count("sslmode")) info += "|sslmode:" + query_map["sslmode"];
    if (query_map.count("cancel")) info += "|cancel:1";

    auto transaction_it = query_map.find("transaction");
    if (transaction_it != query_map.end()) {
        this->set_fixed_addr(true);
        this->set_fixed_conn(true);
        info += "|txn:" + transaction_it->second;
    }

    this->WFComplexClientTask::set_info(info);
    return true;
}

bool ComplexPostgresTask::finish_once()
{
    cleanup_fixed_conn_target(this->is_fixed_conn(), (RouteManager::RouteTarget *)this->target, this->state, this->keep_alive_timeout());

    if (this->state != WFT_STATE_SUCCESS) {
        return true;
    }

    auto *wf_conn = this->WFComplexClientTask::get_connection();
    auto *conn = (PostgresConnection *)(wf_conn ? wf_conn->get_context() : nullptr);

    if (this->get_seq() == 0 && is_ssl_) {
        return handle_starttls_transition(this->get_my_ssl_ctx(), ssl_resp_, verify_full_, uri_, conn, this->state, this->error, wf_conn);
    }

    if (is_ssl_ && conn && conn->state == 1) { 
        conn->state = 2; 
        return false;
    }

    if (is_ssl_ && conn && conn->state == 2) {
        return handle_startup_auth_completion(startup_resp_, conn, is_user_request_, this->state, this->error, wf_conn);
    }

    if (!is_user_request_) {
        return handle_startup_auth_completion(startup_resp_, conn, is_user_request_, this->state, this->error, wf_conn);
    }

    return true; 
}

void ComplexPostgresTask::handle(int state, int error)
{
    bool expected_disconnect = (this->get_req()->is_disconnect() || this->get_req()->is_cancel());
    if (expected_disconnect && state == WFT_STATE_SYS_ERROR && (error == ECONNRESET || error == 0)) {
        state = WFT_STATE_SUCCESS;
        error = 0;
    }

    if (state == WFT_STATE_SUCCESS && this->get_resp()->is_error()) {
        int internal_err = PostgresInternalAccess::get_internal_error(this->get_resp());
        if (internal_err != 0) {
            state = WFT_STATE_TASK_ERROR;
            error = internal_err;
        }
    }

    auto *wf_conn = this->WFComplexClientTask::get_connection();
    auto *conn = (PostgresConnection *)(wf_conn ? wf_conn->get_context() : nullptr);
    update_after_response(conn, this->get_req(), this->get_resp(), state);

    ::WFComplexClientTask<PostgresRequest, PostgresResponse>::handle(state, error);
}

WFConnection *ComplexPostgresTask::get_connection() const
{
    return this->WFComplexClientTask::get_connection();
}

void __postgres_task_set_ssl_ctx(WFPostgresTask *task, void *ssl_ctx)
{
    ((ComplexPostgresTask *)task)->set_my_ssl_ctx((SSL_CTX *)ssl_ctx);
}

} // namespace protocol

WFPostgresTask *WFPostgresTaskFactory::create_postgres_task(const std::string& url,
                                                                      int retry_max,
                                                                      postgres_callback_t callback)
{
    auto *task = new protocol::ComplexPostgresTask(retry_max, std::move(callback));
    ::ParsedURI uri;
    ::URIParser::parse(url, uri);
    task->init(std::move(uri));
    if (task->is_fixed_conn())
        task->set_keep_alive(POSTGRES_KEEPALIVE_TRANSACTION);
    else
        task->set_keep_alive(POSTGRES_KEEPALIVE_DEFAULT);
    return task;
}

WFPostgresTask *WFPostgresTaskFactory::create_postgres_task(const ::ParsedURI& uri,
                                                                      int retry_max,
                                                                      postgres_callback_t callback)
{
    auto *task = new protocol::ComplexPostgresTask(retry_max, std::move(callback));
    task->init(uri);
    if (task->is_fixed_conn())
        task->set_keep_alive(POSTGRES_KEEPALIVE_TRANSACTION);
    else
        task->set_keep_alive(POSTGRES_KEEPALIVE_DEFAULT);
    return task;
}

WFPostgresTask *WFPostgresTaskFactory::create_cancel_task(const std::string& url,
                                                                    int32_t pid,
                                                                    const std::string& secret_data,
                                                                    int retry_max,
                                                                    postgres_callback_t callback)
{
    std::string modified_url = url;
    if (modified_url.find('?') == std::string::npos)
        modified_url += "?cancel=1";
    else
        modified_url += "&cancel=1";

    WFPostgresTask *task = create_postgres_task(modified_url, retry_max, std::move(callback));
    protocol::PostgresInternalAccess::set_cancel(task->get_req(), pid, secret_data);
    task->set_keep_alive(0);
    return task;
}

WFPostgresTask *WFPostgresTaskFactory::create_disconnect_task(
    const std::string& url,
    int retry_max,
    postgres_callback_t callback)
{
    WFPostgresTask *task = create_postgres_task(url, retry_max, std::move(callback));
    protocol::PostgresInternalAccess::set_is_disconnect(task->get_req(), true);
    task->set_keep_alive(0);
    return task;
}

WFPostgresTask *WFPostgresTaskFactory::create_disconnect_task(
    const ParsedURI& uri,
    int retry_max,
    postgres_callback_t callback)
{
    WFPostgresTask *task = create_postgres_task(uri, retry_max, std::move(callback));
    protocol::PostgresInternalAccess::set_is_disconnect(task->get_req(), true);
    task->set_keep_alive(0);
    return task;
}

} // namespace wfpg
