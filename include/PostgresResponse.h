#ifndef _WF_POSTGRES_RESPONSE_H_
#define _WF_POSTGRES_RESPONSE_H_

#include <string>
#include <vector>
#include <map>
#include "workflow/ProtocolMessage.h"
#include "PostgresTypes.h"

namespace wfpg {
class WFPostgresConnection;

namespace protocol {

class PostgresAuth;

class PostgresResponse : public ::protocol::ProtocolMessage
{
    friend class PostgresAuth;
public:
    PostgresResponse() : is_startup_(false), is_error_(false), is_copy_in_(false), is_copy_out_(false),
                         backend_pid_(0), backend_secret_key_(0), transaction_state_('I'),
                         notify_mode_(false), negotiated_protocol_version_(0), cursor_(0), internal_error_(0) {}
    virtual ~PostgresResponse();

    // Public User API
    bool is_error() const { return is_error_; }
    const PostgresError& get_error() const { return error_; }
    const std::string& get_error_msg() const { return error_.message; }
    const std::string& get_sql_state() const { return error_.sql_state; }
    
    const std::map<std::string, std::string>& get_parameters() const { return parameters_; }

    char get_transaction_state() const { return transaction_state_; }
    const std::vector<PostgresNotification>& get_notifications() const { return notifications_; }
    
    const std::vector<PostgresError>& get_notices() const { return notices_; }
    int get_warning_count() const;
    
    std::vector<std::string> get_copy_data() const;
    bool is_copy_done() const;

    int32_t get_backend_pid() const { return backend_pid_; }
    const std::string& get_backend_secret_data() const { return backend_secret_data_; }

    const void *get_buf() const { return buf_.c_str(); }
    size_t get_buf_size() const { return buf_.size(); }

protected:
    virtual int append(const void *buf, size_t *size) override;

private:
    friend class ComplexPostgresTask;
    friend class wfpg::WFPostgresConnection;
    friend class PostgresResultCursor;
    friend struct PostgresInternalAccess;

    void set_error_fatal(const std::string& message) {
        is_error_ = true;
        error_.severity = "FATAL";
        error_.message = message;
    }
    const std::string& get_user() const { return user_; }
    const std::string& get_pass() const { return pass_; }

    // Internal Setters/Getters
    void clear_buffer() {
        buf_.clear();
        cursor_ = 0;
        is_error_ = false;
        is_copy_in_ = false;
        is_copy_out_ = false;
        error_ = PostgresError();
        negotiated_protocol_version_ = 0;
        negotiated_unsupported_options_.clear();
        internal_error_ = 0;
        notices_.clear();
        notifications_.clear();
        parameters_.clear();
    }

    void set_auth(const std::string& user, const std::string& pass);
    void set_is_startup(bool v) { is_startup_ = v; }
    bool is_startup_error() const { return is_error_ && cursor_ > 0; }

    bool is_copy_in() const { return is_copy_in_; }
    bool is_copy_out() const { return is_copy_out_; }

    void set_backend_pid(int32_t pid) { backend_pid_ = pid; }
    int32_t get_backend_secret_key() const { return backend_secret_key_; }
    void set_backend_secret_key(int32_t secret) { backend_secret_key_ = secret; }
    void set_backend_secret_data(const std::string& data) { backend_secret_data_ = data; }
    
    uint32_t get_negotiated_protocol_version() const { return negotiated_protocol_version_; }
    const std::vector<std::string>& get_negotiated_unsupported_options() const { return negotiated_unsupported_options_; }
    
    void set_notify_mode(bool m) { notify_mode_ = m; }
    bool get_notify_mode() const { return notify_mode_; }

    std::string buf_;
    size_t cursor_;
    std::string user_;
    std::string pass_;
    PostgresAuth* auth_ = nullptr;
    
    void set_internal_error(int error) { internal_error_ = error; }
    int get_internal_error() const { return internal_error_; }

    bool is_error_;
    bool is_startup_ = false;
    bool is_copy_in_ = false;
    bool is_copy_out_ = false;
    PostgresError error_;
    
    int32_t backend_pid_;
    int32_t backend_secret_key_;
    std::string backend_secret_data_;
    char transaction_state_; // 'I', 'T', 'E'
    std::map<std::string, std::string> parameters_;
    std::vector<PostgresNotification> notifications_;
    std::vector<PostgresError> notices_;
    bool notify_mode_;
    uint32_t negotiated_protocol_version_;
    std::vector<std::string> negotiated_unsupported_options_;
    int internal_error_{0};
};

} // namespace protocol
} // namespace wfpg

#endif // _WF_POSTGRES_RESPONSE_H_
