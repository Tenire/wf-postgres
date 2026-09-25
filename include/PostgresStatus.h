#ifndef _WF_POSTGRES_STATUS_H_
#define _WF_POSTGRES_STATUS_H_

#include "PostgresTask.h"
#include <string>

namespace wfpg {

enum class PostgresStatusKind {
    SUCCESS,
    TRANSPORT_ERROR,
    SERVER_ERROR,
    EMPTY_RESPONSE
};

class PostgresStatus {
public:
    PostgresStatus()
        : kind_(PostgresStatusKind::SUCCESS),
          transport_state_(WFT_STATE_SUCCESS),
          transport_error_(0) {}

    static PostgresStatus success() {
        return PostgresStatus();
    }

    static PostgresStatus from_transport_error(int state, int error) {
        PostgresStatus s;
        s.kind_ = PostgresStatusKind::TRANSPORT_ERROR;
        s.transport_state_ = state;
        s.transport_error_ = error;
        s.error_message_ = "Transport error (state=" + std::to_string(state) +
                           ", error=" + std::to_string(error) + ")";
        return s;
    }

    static PostgresStatus from_server_error(const protocol::PostgresError& err) {
        PostgresStatus s;
        s.kind_ = PostgresStatusKind::SERVER_ERROR;
        s.server_error_ = err;
        s.sqlstate_ = err.sql_state;
        s.error_message_ = err.message;
        return s;
    }

    static PostgresStatus from_task(const WFPostgresTask* task) {
        if (!task) {
            PostgresStatus s;
            s.kind_ = PostgresStatusKind::EMPTY_RESPONSE;
            s.error_message_ = "Task is null";
            return s;
        }

        int state = task->get_state();
        int error = task->get_error();

        if (state != WFT_STATE_SUCCESS) {
            return from_transport_error(state, error);
        }

        const auto* resp = task->get_resp();
        if (!resp) {
            PostgresStatus s;
            s.kind_ = PostgresStatusKind::EMPTY_RESPONSE;
            s.error_message_ = "Response is null";
            return s;
        }

        if (resp->is_error()) {
            return from_server_error(resp->get_error());
        }

        return success();
    }

    bool ok() const { return kind_ == PostgresStatusKind::SUCCESS; }
    explicit operator bool() const { return ok(); }

    PostgresStatusKind kind() const { return kind_; }
    int transport_state() const { return transport_state_; }
    int transport_error() const { return transport_error_; }
    const std::string& sqlstate() const { return sqlstate_; }
    const std::string& message() const { return error_message_; }
    const protocol::PostgresError& server_error() const { return server_error_; }

    std::string to_string() const {
        if (ok()) return "OK";
        if (kind_ == PostgresStatusKind::SERVER_ERROR) {
            std::string res = "[Server Error]";
            if (!sqlstate_.empty()) res += " SQLSTATE: " + sqlstate_;
            if (!error_message_.empty()) res += " " + error_message_;
            return res;
        }
        if (kind_ == PostgresStatusKind::TRANSPORT_ERROR) {
            return "[Transport Error] state=" + std::to_string(transport_state_) +
                   " error=" + std::to_string(transport_error_) + " (" + error_message_ + ")";
        }
        return "[Empty/Invalid Response] " + error_message_;
    }

private:
    PostgresStatusKind kind_;
    int transport_state_;
    int transport_error_;
    std::string sqlstate_;
    std::string error_message_;
    protocol::PostgresError server_error_;
};

inline PostgresStatus get_status(const WFPostgresTask* task) {
    return PostgresStatus::from_task(task);
}

} // namespace wfpg

#endif // _WF_POSTGRES_STATUS_H_
