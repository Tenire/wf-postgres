#include "PostgresResponse.h"
#include <string.h>
#include <arpa/inet.h>
#include <endian.h>
#include "PostgresStream.h"
#include "PostgresParser.h"
#include "PostgresAuth.h"
#include "PostgresResult.h"
#include "PostgresInternal.h"

namespace wfpg {
namespace protocol {

PostgresResponse::~PostgresResponse() {
    delete auth_;
}

void PostgresResponse::set_auth(const std::string& user, const std::string& pass) {
    user_ = user;
    pass_ = pass;
    if (!auth_) auth_ = new PostgresAuth();
}

int PostgresResponse::append(const void *buf, size_t *size)
{
    size_t len = *size;
    PostgresStream stream_(buf_, cursor_);
    stream_.append(buf, len);
    bool finished = false;

    PostgresFrame frame;
    while (stream_.next_frame(&frame)) {
        if (!frame.payload) {
            is_error_ = true;
            error_.severity = "FATAL";
            error_.message = "Invalid frame length < 4";
            finished = true;
            break;
        }

        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);

        if (event.type == PG_EVENT_ERROR) {
            is_error_ = true;
            PostgresParser::parse_error_response(event.payload, event.payload_size, &error_);
            if (is_startup_) {
                finished = true;
                break;
            }
        } else if (event.type == PG_EVENT_PARAMETER_STATUS) {
            PostgresParser::parse_parameter_status(event.payload, event.payload_size, parameters_);
        } else if (event.type == PG_EVENT_NEGOTIATE_PROTOCOL) {
            PostgresParser::parse_negotiation_response(event.payload, event.payload_size, &negotiated_protocol_version_, &negotiated_unsupported_options_);
        } else if (event.type == PG_EVENT_BACKEND_KEY) {
            PostgresParser::parse_backend_key_data(event.payload, event.payload_size, &backend_pid_, &backend_secret_key_, &backend_secret_data_);
        } else if (event.type == PG_EVENT_NOTIFICATION) {
            PostgresParser::parse_notification(event.payload, event.payload_size, &notifications_);
            if (notify_mode_) {
                finished = true;
                break;
            }
        } else if (event.type == PG_EVENT_NOTICE) {
            PostgresError notice;
            PostgresParser::parse_error_response(event.payload, event.payload_size, &notice);
            notices_.push_back(std::move(notice));
        } else if (event.type == PG_EVENT_AUTH) {
            if (!auth_) auth_ = new PostgresAuth();
            if (auth_->process_auth_request(event.payload, event.payload_size, this) < 0) {
                finished = true;
                break;
            }
        }
        
        if (event.type == PG_EVENT_READY) {
            PostgresParser::parse_ready_for_query(event.payload, event.payload_size, &transaction_state_);
            finished = true;
            break;
        }

        if (event.type == PG_EVENT_COPY_IN) {
            is_copy_in_ = true;
            finished = true;
            break;
        } else if (event.type == PG_EVENT_COPY_OUT) {
            is_copy_out_ = true;
            continue;
        }

        if (event.type == PG_EVENT_COPY_DATA || event.type == PG_EVENT_COPY_DONE) {
            continue;
        }
    }
    
    if (finished) {
        size_t over_read = stream_.buffered_size();
        *size = len - over_read;
        stream_.truncate_unconsumed();
        return 1; 
    }
    
    *size = len;
    return 0;
}

std::vector<std::string> PostgresResponse::get_copy_data() const {
    std::vector<std::string> row;
    if (!is_copy_out_) return row;
    
    size_t tmp_cursor = 0;
    PostgresStream s(buf_, tmp_cursor);
    PostgresFrame frame;
    
    while (s.next_frame(&frame)) {
        if (!frame.payload) return row;
        
        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);
        if (event.type == PG_EVENT_COPY_DATA) { // CopyData
            row.push_back(std::string((const char*)event.payload, event.payload_size));
        } else if (event.type == PG_EVENT_COPY_DONE || event.type == PG_EVENT_COMMAND_COMPLETE || event.type == PG_EVENT_READY) {
            break;
        }
    }
    return row;
}

bool PostgresResponse::is_copy_done() const {
    if (!is_copy_out_) return false;
    
    size_t tmp_cursor = 0;
    PostgresStream s(buf_, tmp_cursor);
    PostgresFrame frame;
    
    while (s.next_frame(&frame)) {
        if (!frame.payload) return false;
        
        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);
        if (event.type == PG_EVENT_COPY_DONE || event.type == PG_EVENT_READY) {
            return true;
        }
    }
    return false;
}

int PostgresSSLResponse::append(const void *buf, size_t *size) {
    if (*size > 0) {
        byte_ = ((const char*)buf)[0];
        *size = 1;
        return 1;
    }
    return 0;
}

int PostgresResponse::get_warning_count() const {
    int count = 0;
    for (const auto& notice : notices_) {
        if (notice.severity == "WARNING") {
            count++;
        }
    }
    return count;
}

} // namespace protocol
} // namespace wfpg
