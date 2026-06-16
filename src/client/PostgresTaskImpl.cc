#include "PostgresTask.h"
#include "workflow/StringUtil.h"
#include "workflow/WFGlobal.h"
#include <openssl/ssl.h>
#include "SSLWrapper.h"

namespace protocol {

static SSL *__create_ssl(SSL_CTX *ssl_ctx)
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

struct PostgresSSLWrapper : public SSLWrapper
{
    PostgresSSLWrapper(ProtocolMessage *msg, SSL *ssl) : SSLWrapper(msg, ssl) { }
    virtual ~PostgresSSLWrapper() { delete this->message; }
    ProtocolMessage *get_msg() { return this->message; }
};

struct PostgresConnection : public WFConnection
{
    int state;
    SSL *ssl;
    SSLWrapper wrapper;

    PostgresConnection(SSL *ssl) : state(0), wrapper(&wrapper, ssl)
    {
        this->ssl = ssl;
    }
};

bool ComplexPostgresTask::check_request()
{
    return true;
}

CommMessageOut *ComplexPostgresTask::message_out()
{
    PostgresRequest *req;
    is_user_request_ = false;

    if (this->get_seq() == 0) {
        if (is_ssl_) {
            return new PostgresSSLRequest();
        } else {
            req = new PostgresRequest();
            req->set_is_startup(true);
            req->set_auth(user_, db_, pass_);
            return req;
        }
    }

    auto *conn = (PostgresConnection *)this->get_connection();
    
    if (is_ssl_ && conn->state == 1) { // SSL negotiated, send SSL Handshake
        return new SSLHandshaker(conn->ssl);
    } else if (is_ssl_ && conn->state == 2) { // SSL handshake done, send StartupMessage
        req = new PostgresRequest();
        req->set_is_startup(true);
        req->set_auth(user_, db_, pass_);
        conn->wrapper = SSLWrapper(req, conn->ssl);
        return &conn->wrapper;
    }

    // Valid authenticated connection: Query
    is_user_request_ = true;
    req = this->get_req();
    req->set_is_startup(false);

    if (is_ssl_) {
        conn->wrapper = SSLWrapper(req, conn->ssl);
        return &conn->wrapper;
    }
    return req;
}

CommMessageIn *ComplexPostgresTask::message_in()
{
    PostgresResponse *resp;

    if (this->get_seq() == 0) {
        if (is_ssl_) {
            return new PostgresSSLResponse();
        } else {
            resp = new PostgresResponse();
            resp->clear_buffer();
            resp->set_auth(user_, pass_);
            resp->set_is_startup(true);
            return resp;
        }
    }

    auto *conn = (PostgresConnection *)this->get_connection();

    if (is_ssl_ && conn->state == 1) { // SSL request sent, expecting S or N? Wait, response for seq==0 is PostgresSSLResponse. seq==1 is handshaker.
        return new SSLHandshaker(conn->ssl);
    } else if (is_ssl_ && conn->state == 2) { // Sending StartupMessage
        resp = new PostgresResponse();
        resp->clear_buffer();
        resp->set_auth(user_, pass_);
        resp->set_is_startup(true);
        conn->wrapper = SSLWrapper(resp, conn->ssl);
        return &conn->wrapper;
    }

    // User query response
    resp = this->get_resp();
    resp->clear_buffer();
    resp->set_auth(user_, pass_);
    resp->set_is_startup(false);

    if (is_ssl_) {
        conn->wrapper = SSLWrapper(resp, conn->ssl);
        return &conn->wrapper;
    }
    return resp;
}

int ComplexPostgresTask::keep_alive_timeout()
{
    return 60 * 1000;
}

int ComplexPostgresTask::first_timeout()
{
    return this->watch_timeo;
}

bool ComplexPostgresTask::init_success()
{
    if (uri_.scheme && strcasecmp(uri_.scheme, "postgresqls") == 0) {
        is_ssl_ = true;
    } else if (uri_.scheme && (strcasecmp(uri_.scheme, "postgres") == 0 || strcasecmp(uri_.scheme, "postgresql") == 0)) {
        is_ssl_ = false;
        if (uri_.query && strcasestr(uri_.query, "sslmode=require")) {
            is_ssl_ = true;
        }
    } else {
        this->state = WFT_STATE_TASK_ERROR;
        this->error = WFT_ERR_URI_SCHEME_INVALID;
        return false;
    }

    if (uri_.userinfo) {
        const char *colon = strchr(uri_.userinfo, ':');
        if (colon) {
            user_.assign(uri_.userinfo, colon - uri_.userinfo);
            pass_.assign(colon + 1);
        } else {
            user_.assign(uri_.userinfo);
        }
    }

    if (uri_.path && uri_.path[0] == '/' && uri_.path[1]) {
        db_.assign(uri_.path + 1);
    }

    StringUtil::url_decode(user_);
    StringUtil::url_decode(pass_);
    StringUtil::url_decode(db_);

    // Grouping criteria for connection pool
    std::string info = std::string(is_ssl_ ? "postgresqls" : "postgres") + "|user:" + user_ + "|pass:" + pass_ + "|db:" + db_;
    if (uri_.host) info += "|host:" + std::string(uri_.host);
    if (uri_.port) info += "|port:" + std::string(uri_.port);

    if (uri_.query) {
        std::string query_str(uri_.query);
        auto add_query_param = [&](const std::string& key) {
            std::string prefix = key + "=";
            size_t pos = query_str.find(prefix);
            while (pos != std::string::npos) {
                if (pos == 0 || query_str[pos - 1] == '&') {
                    size_t end = query_str.find('&', pos);
                    std::string val = query_str.substr(pos + prefix.size(), end == std::string::npos ? std::string::npos : end - (pos + prefix.size()));
                    StringUtil::url_decode(val);
                    info += "|" + key + ":" + val;
                    break;
                }
                pos = query_str.find(prefix, pos + prefix.size());
            }
        };
        add_query_param("sslmode");
        add_query_param("application_name");
        add_query_param("options");
        add_query_param("transaction");
    }

    this->WFComplexClientTask::set_info(info);

    return true;
}

bool ComplexPostgresTask::finish_once()
{
    auto *conn = (PostgresConnection *)this->get_connection();

    if (this->get_seq() == 0 && is_ssl_) {
        auto *resp = (PostgresSSLResponse *)this->get_resp();
        if (resp->get_byte() == 'S') {
            static SSL_CTX *ssl_ctx = WFGlobal::get_ssl_client_ctx();
            SSL *ssl = __create_ssl(ssl_ctx);
            if (!ssl) {
                state = WFT_STATE_SYS_ERROR;
                error = errno;
                return true;
            }
            SSL_set_connect_state(ssl);

            auto *my_conn = new PostgresConnection(ssl);
            my_conn->state = 1; // Handshake next
            this->WFComplexClientTask::get_connection()->set_context(my_conn, [](void *ctx) {
                delete (PostgresConnection *)ctx;
            });
            return false; // Continue with seq=1
        } else {
            state = WFT_STATE_TASK_ERROR;
            error = WFT_ERR_MYSQL_SSL_NOT_SUPPORTED; // Re-use or define our own
            return true; // Abort
        }
    }

    if (is_ssl_ && conn->state == 1) { // SSLHandshaker finished
        if (this->state != WFT_STATE_SUCCESS)
            return true;
        
        conn->state = 2; // Send StartupMessage next
        return false;
    }

    if (is_ssl_ && conn->state == 2) {
        if (!is_user_request_) {
            // StartupMessage finished sending and response parsed
            conn->state = 3; // Ready
            is_user_request_ = true;
            return false;
        }
    }

    if (!is_user_request_) {
        is_user_request_ = true;
        return false; // return false to force re-dispatching
    }

    return true; // Finished
}

WFConnection *ComplexPostgresTask::get_connection() const
{
    WFConnection *conn = this->WFComplexClientTask::get_connection();
    if (conn) {
        void *ctx = conn->get_context();
        if (ctx) return (PostgresConnection *)ctx;
    }
    return conn;
}

} // namespace protocol

protocol::WFPostgresTask *WFPostgresTaskFactory::create_postgres_task(const std::string& url,
                                                                      int retry_max,
                                                                      protocol::postgres_callback_t callback)
{
    auto *task = new protocol::ComplexPostgresTask(retry_max, std::move(callback));
    ParsedURI uri;
    URIParser::parse(url, uri);
    task->init(std::move(uri));
    task->set_keep_alive(60 * 1000);
    return task;
}

protocol::WFPostgresTask *WFPostgresTaskFactory::create_postgres_task(const ParsedURI& uri,
                                                                      int retry_max,
                                                                      protocol::postgres_callback_t callback)
{
    auto *task = new protocol::ComplexPostgresTask(retry_max, std::move(callback));
    ParsedURI uri_copy = uri;
    if (uri.query) uri_copy.query = strdup(uri.query);
    if (uri.port) uri_copy.port = strdup(uri.port);
    if (uri.path) uri_copy.path = strdup(uri.path);
    if (uri.userinfo) uri_copy.userinfo = strdup(uri.userinfo);
    if (uri.host) uri_copy.host = strdup(uri.host);
    if (uri.scheme) uri_copy.scheme = strdup(uri.scheme);
    if (uri.fragment) uri_copy.fragment = strdup(uri.fragment);
    if (uri.state) uri_copy.state = uri.state;
    if (uri.error) uri_copy.error = uri.error;

    task->init(std::move(uri_copy));
    task->set_keep_alive(60 * 1000);
    return task;
}

namespace protocol {

void ComplexPostgresTask::handle(int state, int error)
{
    if (this->get_req()->is_disconnect() && state == WFT_STATE_SYS_ERROR && error == ECONNRESET) {
        state = WFT_STATE_SUCCESS;
        error = 0;
    }
    WFComplexClientTask<PostgresRequest, PostgresResponse>::handle(state, error);
}

}
