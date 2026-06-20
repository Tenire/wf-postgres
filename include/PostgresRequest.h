#ifndef _WF_POSTGRES_REQUEST_H_
#define _WF_POSTGRES_REQUEST_H_

#include <string>
#include <vector>
#include <map>
#include "workflow/ProtocolMessage.h"
#include "PostgresTypes.h"

namespace wfpg {
class WFPostgresConnection;

namespace protocol {

class PostgresRequest : public ::protocol::ProtocolMessage
{
public:
    PostgresRequest() = default;
    virtual ~PostgresRequest() = default;

    // Public User API
    void set_query(const std::string& query) { query_ = query; }
    const std::string& get_query() const { return query_; }
    
    void set_query(const std::string& query, const std::vector<std::string>& params) {
        query_ = query;
        params_.clear();
        for (const auto& p : params) params_.push_back(PostgresParameter(p));
        has_params_ = true;
    }
    
    void set_query(const std::string& query, const std::vector<PostgresParameter>& params) {
        query_ = query;
        params_ = params;
        has_params_ = true;
    }

    void set_statement_name(const std::string& name) { statement_name_ = name; }
    const std::string& get_statement_name() const { return statement_name_; }

    void set_portal_name(const std::string& name) { portal_name_ = name; }
    const std::string& get_portal_name() const { return portal_name_; }

    void set_skip_parse(bool skip) { skip_parse_ = skip; }
    bool skip_parse() const { return skip_parse_; }

    void set_result_format(int format) { result_format_ = format; }
    int get_result_format() const { return result_format_; }

    void set_copy_data(const std::string& data, bool is_done = false) {
        is_copy_ = true;
        copy_data_ = data;
        copy_done_ = is_done;
    }
    void set_copy_fail(const std::string& error_msg) {
        is_copy_ = true;
        copy_fail_ = true;
        copy_data_ = error_msg;
    }

    void set_wait_notification(bool wait) { wait_notification_ = wait; }

protected:
    virtual int encode(struct iovec vectors[], int max) override;
    virtual int append(const void *buf, size_t *size) override;

private:
    friend class ComplexPostgresTask;
    friend class wfpg::WFPostgresConnection;
    friend struct PostgresInternalAccess;

    // Internal Setters (Hidden from User API)
    void set_is_startup(bool startup) { is_startup_ = startup; }
    bool is_startup() const { return is_startup_; }

    void set_is_disconnect(bool disconnect) { is_disconnect_ = disconnect; }
    bool is_disconnect() const { return is_disconnect_; }

    void set_auth(const std::string& user, const std::string& db, const std::string& pass) {
        user_ = user;
        db_ = db;
        pass_ = pass;
    }
    const std::string& get_user() const { return user_; }
    const std::string& get_db() const { return db_; }
    const std::string& get_password() const { return pass_; }

    void set_cancel(int32_t pid, const std::string& secret_data) {
        is_cancel_ = true;
        cancel_pid_ = pid;
        cancel_secret_data_ = secret_data;
    }
    bool is_cancel() const { return is_cancel_; }

    bool is_copy() const { return is_copy_; }
    bool is_copy_done() const { return copy_done_; }
    bool is_copy_fail() const { return copy_fail_; }
    bool is_wait_notification() const { return wait_notification_; }

    void set_protocol_version(uint32_t version) { protocol_version_ = version; }
    uint32_t get_protocol_version() const { return protocol_version_; }

    void set_startup_params(const std::map<std::string, std::string>& params) { startup_params_ = params; }

protected:
    int encode_terminate(struct iovec vectors[]);
    int encode_cancel(struct iovec vectors[]);
    int encode_copy(struct iovec vectors[]);
    int encode_startup(struct iovec vectors[]);
    int encode_simple_query(struct iovec vectors[]);
    int encode_extended_query(struct iovec vectors[]);

    std::string buf_;
    bool is_startup_ = false;
    bool is_disconnect_ = false;
    bool is_cancel_ = false;
    bool is_copy_ = false;
    bool copy_done_ = false;
    bool copy_fail_ = false;
    bool wait_notification_ = false;
    bool has_params_ = false;
    uint32_t protocol_version_ = 196610; // Default 3.2
    std::map<std::string, std::string> startup_params_;
    int result_format_ = 0; // 0 = text, 1 = binary
    int32_t cancel_pid_ = 0;
    std::string cancel_secret_data_;
    bool skip_parse_ = false;
    std::string copy_data_;
    std::string statement_name_;
    std::string portal_name_;
    std::string query_;
    std::vector<PostgresParameter> params_;
    std::string user_;
    std::string db_;
    std::string pass_;
};

} // namespace protocol
} // namespace wfpg

#endif // _WF_POSTGRES_REQUEST_H_
