/*
  PostgreSQL Backend Message Parser & Event Classification.
  References:
    - PostgreSQL Protocol Specification: Message Formats
      https://www.postgresql.org/docs/current/protocol-message-formats.html
    - PostgreSQL Protocol Specification: Formats and Conventions
      https://www.postgresql.org/docs/current/protocol-overview.html
*/

#include "PostgresParser.h"
#include "PostgresWireUtil.h"
#include <arpa/inet.h>
#include <string.h>
#include <endian.h>

namespace wfpg {
namespace protocol {

PostgresBackendEvent PostgresParser::parse_backend_event(const PostgresFrame& frame) {
    PostgresBackendEvent event;
    event.payload = frame.payload;
    event.payload_size = frame.payload_size;
    
    switch (frame.type) {
        case 'R': event.type = PG_EVENT_AUTH; break;
        case 'E': event.type = PG_EVENT_ERROR; break;
        case 'S': event.type = PG_EVENT_PARAMETER_STATUS; break;
        case 'K': event.type = PG_EVENT_BACKEND_KEY; break;
        case 'v': event.type = PG_EVENT_NEGOTIATE_PROTOCOL; break;
        case 'Z': event.type = PG_EVENT_READY; break;
        case 'A': event.type = PG_EVENT_NOTIFICATION; break;
        case 'T': event.type = PG_EVENT_ROW_DESCRIPTION; break;
        case 'D': event.type = PG_EVENT_DATA_ROW; break;
        case 'C': event.type = PG_EVENT_COMMAND_COMPLETE; break;
        case 'G': event.type = PG_EVENT_COPY_IN; break;
        case 'H': event.type = PG_EVENT_COPY_OUT; break;
        case 'W': event.type = PG_EVENT_COPY_OUT; break;
        case 'd': event.type = PG_EVENT_COPY_DATA; break;
        case 'c': event.type = PG_EVENT_COPY_DONE; break;
        case 'f': event.type = PG_EVENT_COPY_FAIL; break;
        case 'N': event.type = PG_EVENT_NOTICE; break;
        case 'I': event.type = PG_EVENT_EMPTY_QUERY; break;
        case '1': event.type = PG_EVENT_PARSE_COMPLETE; break;
        case '2': event.type = PG_EVENT_BIND_COMPLETE; break;
        case '3': event.type = PG_EVENT_CLOSE_COMPLETE; break;
        case 't': event.type = PG_EVENT_PARAMETER_DESCRIPTION; break;
        case 'n': event.type = PG_EVENT_NO_DATA; break;
        default:  event.type = PG_EVENT_UNKNOWN; break;
    }
    return event;
}

int PostgresParser::parse_error_response(const uint8_t *data, size_t len, PostgresError *error) {
    if (!error) return -1;
    const uint8_t *end = data + len;
    while (data < end && *data != '\0') {
        char type = *data++;
        std::string value;
        if (!PostgresWireUtil::read_cstring(data, end, &value)) {
            break;
        }
        if (type == 'S') error->severity = value;
        else if (type == 'V') { if (error->severity.empty()) error->severity = value; }
        else if (type == 'C') error->sql_state = value;
        else if (type == 'M') error->message = value;
        else if (type == 'D') error->detail = value;
        else if (type == 'H') error->hint = value;
        else if (type == 'P') error->position = value;
    }
    return 0;
}

int PostgresParser::parse_parameter_status(const uint8_t *data, size_t len, std::map<std::string, std::string>& parameters) {
    const uint8_t *end = data + len;
    std::string key, value;
    if (PostgresWireUtil::read_cstring(data, end, &key)) {
        if (PostgresWireUtil::read_cstring(data, end, &value)) {
            parameters[key] = value;
        }
    }
    return 0;
}

int PostgresParser::parse_backend_key_data(const uint8_t *data, size_t len, int32_t *pid, int32_t *secret_key, std::string *secret_data) {
    if (len >= 8) {
        if (pid) *pid = PostgresWireUtil::read_uint32(data);
        if (secret_key) *secret_key = PostgresWireUtil::read_uint32(data + 4);
        if (secret_data) {
            secret_data->assign((const char*)(data + 4), len - 4);
        }
    }
    return 0;
}

int PostgresParser::parse_notification(const uint8_t *data, size_t len, std::vector<PostgresNotification> *notifications) {
    if (len >= 4 && notifications) {
        uint32_t pid = PostgresWireUtil::read_uint32(data);
        const uint8_t *p = data + 4;
        const uint8_t *end = data + len;
        
        std::string channel, payload;
        if (PostgresWireUtil::read_cstring(p, end, &channel)) {
            if (PostgresWireUtil::read_cstring(p, end, &payload)) {
                notifications->push_back({pid, channel, payload});
            }
        }
    }
    return 0;
}

int PostgresParser::parse_negotiation_response(const uint8_t *data, size_t len, uint32_t *version, std::vector<std::string> *unsupported_options) {
    if (len >= 8) {
        if (version) *version = PostgresWireUtil::read_uint32(data);
        const uint8_t *p = data + 4;
        const uint8_t *end = data + len;
        uint32_t num_options = PostgresWireUtil::read_uint32(p);
        p += 4;
        if (unsupported_options) {
            for (uint32_t i = 0; i < num_options && p < end; ++i) {
                std::string option;
                if (PostgresWireUtil::read_cstring(p, end, &option)) {
                    unsupported_options->push_back(option);
                } else {
                    break;
                }
            }
        }
    }
    return 0;
}

int PostgresParser::parse_row_description(const uint8_t *data, size_t len, std::vector<PostgresField> *fields) {
    if (len < 2 || !fields) return -1;
    fields->clear();
    int16_t num_fields = PostgresWireUtil::read_int16(data);
    const uint8_t *ptr = data + 2;
    const uint8_t *frame_end = data + len;
    for (int i = 0; i < num_fields; i++) {
        PostgresField f;
        if (!PostgresWireUtil::read_cstring(ptr, frame_end, &f.name)) {
            fields->clear();
            return -1;
        }
        if (frame_end - ptr < 18) {
            fields->clear();
            return -1;
        }
        f.table_oid = PostgresWireUtil::read_uint32(ptr); ptr += 4;
        f.column_attr = PostgresWireUtil::read_uint16(ptr); ptr += 2;
        f.type_oid = PostgresWireUtil::read_uint32(ptr); ptr += 4;
        f.type_size = PostgresWireUtil::read_uint16(ptr); ptr += 2;
        f.type_modifier = PostgresWireUtil::read_uint32(ptr); ptr += 4;
        f.format = PostgresWireUtil::read_uint16(ptr); ptr += 2;
        fields->push_back(f);
    }
    return 0;
}

int PostgresParser::parse_command_complete(const uint8_t *data, size_t len, std::string *command_tag) {
    if (len > 0 && command_tag) {
        const uint8_t* null_pos = (const uint8_t*)memchr(data, '\0', len);
        if (null_pos) {
            *command_tag = std::string((const char*)data, null_pos - data);
        }
    }
    return 0;
}

int PostgresParser::parse_data_row(const uint8_t *data, size_t len, std::vector<PostgresCell> *row, const std::vector<PostgresField>& fields) {
    if (len < 2 || !row) return -1;
    int16_t num_cols = PostgresWireUtil::read_int16(data);
    const uint8_t *p = data + 2;
    const uint8_t *end = data + len;
    
    if (num_cols < 0 || (size_t)num_cols > fields.size()) return -1;

    for (int i = 0; i < num_cols; i++) {
        if (p + 4 > end) return -1;
        int32_t col_len = PostgresWireUtil::read_int32(p);
        p += 4;
        
        const PostgresField *field_ptr = &fields[i];
        
        if (col_len == -1) {
            row->push_back({nullptr, 0, true, field_ptr});
        } else if (col_len >= 0) {
            if ((size_t)(end - p) < (size_t)col_len) return -1;
            row->push_back({p, (size_t)col_len, false, field_ptr});
            p += col_len;
        } else {
            return -1;
        }
    }
    return 0;
}

int PostgresParser::parse_ready_for_query(const uint8_t *data, size_t len, char *transaction_state) {
    if (len == 1 && transaction_state) {
        *transaction_state = data[0];
    }
    return 0;
}

} // namespace protocol
} // namespace wfpg
