#ifndef _WF_POSTGRES_INTERNAL_H_
#define _WF_POSTGRES_INTERNAL_H_

#include "PostgresRequest.h"
#include "PostgresResponse.h"
#include "PostgresTask.h"

namespace wfpg {
namespace protocol {

struct PostgresInternalAccess {
    static void set_auth(PostgresRequest* req, const std::string& user, const std::string& db, const std::string& pass) {
        req->set_auth(user, db, pass);
    }
    static void set_is_startup(PostgresRequest* req, bool startup) {
        req->set_is_startup(startup);
    }
    static void set_cancel(PostgresRequest* req, int32_t pid, const std::string& secret) {
        req->set_cancel(pid, secret);
    }
    static void set_protocol_version(PostgresRequest* req, uint32_t version) {
        req->set_protocol_version(version);
    }
    static void set_is_disconnect(PostgresRequest* req, bool disconnect) {
        req->set_is_disconnect(disconnect);
    }
    static void set_copy_data(PostgresRequest* req, const std::string& data, bool is_done) {
        req->set_copy_data(data, is_done);
    }
    static bool is_wait_notification(const PostgresRequest* req) {
        return req->is_wait_notification();
    }
    static void set_wait_notification(PostgresRequest* req, bool v) {
        req->set_wait_notification(v);
    }
    static bool is_copy(const PostgresRequest* req) {
        return req->is_copy();
    }
    static bool is_copy_done(const PostgresRequest* req) {
        return req->is_copy_done();
    }
    static bool is_copy_fail(const PostgresRequest* req) {
        return req->is_copy_fail();
    }
    static bool is_disconnect(const PostgresRequest* req) {
        return req->is_disconnect();
    }
    static void set_startup_params(PostgresRequest *req, const std::map<std::string, std::string>& params) {
        req->set_startup_params(params);
    }
    
    // Response helpers
    static void set_auth(PostgresResponse* resp, const std::string& user, const std::string& pass) {
        resp->set_auth(user, pass);
    }
    static void set_is_startup(PostgresResponse* resp, bool startup) {
        resp->set_is_startup(startup);
    }
    static bool is_copy_in(const PostgresResponse* resp) {
        return resp->is_copy_in();
    }
    static void set_notify_mode(PostgresResponse* resp, bool m) {
        resp->set_notify_mode(m);
    }
    static void set_internal_error(PostgresResponse *resp, int error) {
        resp->set_internal_error(error);
    }
    static int get_internal_error(const PostgresResponse *resp) {
        return resp->get_internal_error();
    }
    static bool get_notify_mode(const PostgresResponse* resp) {
        return resp->get_notify_mode();
    }
    static int32_t get_backend_pid(const PostgresResponse* resp) {
        return resp->get_backend_pid();
    }
    static int32_t get_backend_secret_key(const PostgresResponse* resp) {
        return resp->get_backend_secret_key();
    }
    static uint32_t get_negotiated_protocol_version(const PostgresResponse* resp) {
        return resp->get_negotiated_protocol_version();
    }
    static std::vector<std::string> get_negotiated_unsupported_options(const PostgresResponse* resp) {
        return resp->get_negotiated_unsupported_options();
    }
    static std::string get_backend_secret_data(const PostgresResponse* resp) {
        return resp->get_backend_secret_data();
    }
    static void set_backend_pid(PostgresResponse* resp, int32_t pid) {
        resp->set_backend_pid(pid);
    }
    static void set_backend_secret_key(PostgresResponse* resp, int32_t secret) {
        resp->set_backend_secret_key(secret);
    }
    static void set_backend_secret_data(PostgresResponse* resp, const std::string& data) {
        resp->set_backend_secret_data(data);
    }
};

// Internal specific requests
class PostgresSSLRequest : public PostgresRequest
{
protected:
    virtual int encode(struct iovec vectors[], int max) override;
};

// Internal SSL Response
class PostgresSSLResponse : public ::protocol::ProtocolMessage
{
public:
    char get_byte() const { return byte_; }
protected:
    virtual int append(const void *buf, size_t *size) override;
private:
    char byte_ = 0;
};

void __postgres_task_set_ssl_ctx(WFPostgresTask *task, void *ssl_ctx);

} // namespace protocol
} // namespace wfpg

#endif
