#ifndef _WF_POSTGRES_MESSAGE_H_
#define _WF_POSTGRES_MESSAGE_H_

#include <string>
#include <vector>
#include "workflow/ProtocolMessage.h"
#include "ScramAuth.h"

namespace protocol {

// Base class for all PostgreSQL messages
class PostgresMessage : public ProtocolMessage
{
public:
    PostgresMessage() {}
    virtual ~PostgresMessage() {}

protected:
    virtual int encode(struct iovec vectors[], int max) override;
    virtual int append(const void *buf, size_t *size) override;

    // Buffer to hold raw message data, either for assembling incomplete frames
    // or holding the outgoing payload.
    // In a production optimization, we might use string_view or non-copying buffers,
    // but a standard string is robust for the initial parser state machine.
    std::string buf_;
};

// Represents a frontend request sent to PostgreSQL
class PostgresRequest : public PostgresMessage
{
public:
    PostgresRequest() : is_startup_(false) {}

    void set_is_startup(bool startup) { is_startup_ = startup; }
    bool is_startup() const { return is_startup_; }

    void set_query(const std::string& query) { query_ = query; }
    const std::string& get_query() const { return query_; }
    void set_query(const std::string& query, const std::vector<std::string>& params) {
        query_ = query;
        params_ = params;
        has_params_ = true;
    }

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

    void set_result_format(int format) { result_format_ = format; }
    int get_result_format() const { return result_format_; }

protected:
    virtual int encode(struct iovec vectors[], int max) override;

private:
    bool is_startup_;
    bool is_disconnect_ = false;
    bool has_params_ = false;
    int result_format_ = 0; // 0 = text, 1 = binary
    std::string query_;
    std::vector<std::string> params_;
    std::string user_;
    std::string db_;
    std::string pass_;
};

// Holds parsed fields from an ErrorResponse (E) frame
struct PostgresError {
    std::string severity;
    std::string sql_state;
    std::string message;
    std::string detail;
    std::string hint;
    std::string position;
};

// Represents a backend response received from PostgreSQL
class PostgresResponse : public PostgresMessage
{
public:
    PostgresResponse() : cursor_(0), is_error_(false) {}

    void set_auth(const std::string& user, const std::string& pass) {
        user_ = user;
        pass_ = pass;
        scram_auth_.init(user, pass);
    }

    const void *get_buf() const { return buf_.c_str(); }
    size_t get_buf_size() const { return buf_.size(); }

    void clear_buffer() {
        buf_.clear();
        cursor_ = 0;
        is_error_ = false;
        error_ = PostgresError();
    }

    bool is_error() const { return is_error_; }
    const PostgresError& get_error() const { return error_; }
    
    void set_is_startup(bool v) { is_startup_ = v; }

    // Check if response parsing stopped early due to a startup error
    bool is_startup_error() const { return is_error_ && cursor_ > 0; }

protected:
    virtual int append(const void *buf, size_t *size) override;

private:
    size_t cursor_;
    std::string user_;
    std::string pass_;
    ScramAuth scram_auth_;
    
    bool is_error_;
    bool is_startup_ = false;
    PostgresError error_;
};

class PostgresSSLRequest : public PostgresMessage
{
protected:
    virtual int encode(struct iovec vectors[], int max) override;
};

class PostgresSSLResponse : public PostgresMessage
{
public:
    char get_byte() const { return byte_; }
protected:
    virtual int append(const void *buf, size_t *size) override;
private:
    char byte_ = 0;
};

} // namespace protocol

#endif // _WF_POSTGRES_MESSAGE_H_
