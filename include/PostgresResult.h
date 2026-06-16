#ifndef _WF_POSTGRES_RESULT_H_
#define _WF_POSTGRES_RESULT_H_

#include "PostgresMessage.h"
#include <string>
#include <vector>
#include <string.h>
#include <arpa/inet.h>
#include <endian.h>

namespace protocol {

// Represents a single field description in a RowDescription message
struct PostgresField {
    std::string name;
    int32_t table_oid;
    int16_t column_attr;
    int32_t type_oid;
    int16_t type_size;
    int32_t type_modifier;
    int16_t format;
};

namespace PostgresOid {
    const int32_t BOOL = 16;
    const int32_t BYTEA = 17;
    const int32_t CHAR = 18;
    const int32_t NAME = 19;
    const int32_t INT8 = 20;
    const int32_t INT2 = 21;
    const int32_t INT4 = 23;
    const int32_t TEXT = 25;
    const int32_t OID = 26;
    const int32_t JSON = 114;
    const int32_t FLOAT4 = 700;
    const int32_t FLOAT8 = 701;
    const int32_t BPCHAR = 1042;
    const int32_t VARCHAR = 1043;
    const int32_t DATE = 1082;
    const int32_t TIME = 1083;
    const int32_t TIMESTAMP = 1114;
    const int32_t TIMESTAMPTZ = 1184;
    const int32_t NUMERIC = 1700;
    const int32_t UUID = 2950;
    const int32_t JSONB = 3802;
}

// Represents a parsed value. is_null is true if length was -1.
struct PostgresCell {
    const void *data;
    size_t length;
    bool is_null;
    const PostgresField *field;

    // Type Extraction Methods
    long long as_int() const {
        if (is_null || length == 0) return 0;
        if (field->format == 1) { // binary
            if (length == 2) {
                int16_t v;
                memcpy(&v, data, 2);
                return ntohs(v);
            } else if (length == 4) {
                int32_t v;
                memcpy(&v, data, 4);
                return ntohl(v);
            } else if (length == 8) {
                int64_t v;
                memcpy(&v, data, 8);
                return be64toh(v);
            }
            return 0; // fallback
        }
        return std::stoll(std::string((const char*)data, length));
    }

    int64_t as_bigint() const {
        if (is_null) return 0;
        if (field->format == 1) {
            uint64_t v;
            memcpy(&v, data, sizeof(uint64_t));
            // Postgres uses network byte order (big endian) for binary format
            // We need a portable 64-bit ntoh, or use compiler builtins.
            // Using __builtin_bswap64 if little endian, but let's implement a manual one
            // just to be strictly portable without <endian.h>
            uint32_t high = ntohl(v >> 32);
            uint32_t low = ntohl(v & 0xFFFFFFFF);
            return ((uint64_t)low << 32) | high;
        }
        return std::stoll(std::string((const char*)data, length));
    }

    double as_double() const {
        if (is_null) return 0.0;
        if (field->format == 1) {
            uint64_t v;
            memcpy(&v, data, sizeof(uint64_t));
            uint32_t high = ntohl(v >> 32);
            uint32_t low = ntohl(v & 0xFFFFFFFF);
            v = ((uint64_t)low << 32) | high;
            double d;
            memcpy(&d, &v, sizeof(double));
            return d;
        }
        return std::stod(std::string((const char*)data, length));
    }

    float as_float() const {
        if (is_null) return 0.0f;
        if (field->format == 1) {
            uint32_t v;
            memcpy(&v, data, sizeof(uint32_t));
            v = ntohl(v);
            float f;
            memcpy(&f, &v, sizeof(float));
            return f;
        }
        return std::stof(std::string((const char*)data, length));
    }

    std::string as_datetime_string() const {
        if (is_null) return "";
        if (field->format == 1) {
            // Postgres uses int64 microseconds since 2000-01-01
            int64_t v = as_bigint();
            time_t epoch = (v / 1000000LL) + 946684800LL;
            int micros = v % 1000000LL;
            if (micros < 0) {
                micros += 1000000LL;
                epoch -= 1;
            }
            struct tm t;
            gmtime_r(&epoch, &t); // Using UTC time
            char buf[64];
            if (micros > 0) {
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06d",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, micros);
            } else {
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec);
            }
            return std::string(buf);
        }
        return std::string((const char*)data, length);
    }

    std::string as_date_string() const {
        if (is_null) return "";
        if (field->format == 1) {
            // Postgres uses int32 days since 2000-01-01
            int32_t v = as_int();
            time_t epoch = (v * 86400LL) + 946684800LL;
            struct tm t;
            gmtime_r(&epoch, &t);
            char buf[32];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
            return std::string(buf);
        }
        return std::string((const char*)data, length);
    }

    std::string as_string() const {
        if (is_null) return "";
        return std::string((const char*)data, length);
    }

    std::string as_uuid_string() const {
        if (is_null) return "";
        if (field->format == 1) { // Binary UUID is 16 bytes
            if (length != 16) return "";
            char buf[37];
            const unsigned char *u = (const unsigned char*)data;
            snprintf(buf, sizeof(buf), 
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
                u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
            return std::string(buf);
        }
        return std::string((const char*)data, length);
    }

    bool as_bool() const {
        if (is_null || length == 0) return false;
        if (field->format == 1) {
            return *((const uint8_t*)data) != 0;
        }
        char c = *((const char*)data);
        return c == 't' || c == 'T' || c == '1';
    }
};

class PostgresResultCursor
{
public:
    PostgresResultCursor(PostgresResponse *resp)
    {
        // Copy the body or hold a reference.
        // For performance, we hold a reference to the response buffer.
        head_ = (const uint8_t *)resp->get_buf();
        end_ = head_ + resp->get_buf_size();
        cursor_ = head_;
        
        parse_metadata();
    }

    // Move to the next row in the current result set. Returns false if no more rows.
    bool fetch_row(std::vector<PostgresCell>& row)
    {
        if (current_result_set_done_ || cursor_ >= end_) return false;
        
        while (cursor_ + 5 <= end_) {
            char type = *cursor_;
            uint32_t len;
            memcpy(&len, cursor_ + 1, 4);
            len = ntohl(len);

            if (len < 4 || (size_t)(end_ - cursor_) < 1 + (size_t)len) {
                return false; // Malformed frame length or out of bounds
            }

            if (type == 'D') { // DataRow
                if (len < 6) return false;
                int16_t num_cols;
                memcpy(&num_cols, cursor_ + 5, 2);
                num_cols = ntohs(num_cols);

                if (num_cols < 0 || (size_t)num_cols > fields_.size()) return false;

                row.clear();
                row.reserve(num_cols);

                const uint8_t *p = cursor_ + 7;
                const uint8_t *frame_end = cursor_ + 1 + len;
                for (int i = 0; i < num_cols; i++) {
                    if ((size_t)(frame_end - p) < 4) return false; // Malformed data length

                    int32_t col_len;
                    memcpy(&col_len, p, 4);
                    col_len = ntohl(col_len);
                    p += 4;

                    if (col_len == -1) {
                        row.push_back({nullptr, 0, true, &fields_[i]});
                    } else if (col_len >= 0) {
                        if ((size_t)(frame_end - p) < (size_t)col_len) return false; // Malformed column data
                        row.push_back({p, (size_t)col_len, false, &fields_[i]});
                        p += col_len;
                    } else {
                        return false; // Invalid negative length
                    }
                }
                
                cursor_ += 1 + len;
                return true;
            } else if (type == 'C') { // CommandComplete
                if (len >= 5) {
                    const uint8_t* null_pos = (const uint8_t*)memchr(cursor_ + 5, '\0', len - 4);
                    if (null_pos) {
                        command_tag_ = std::string((const char*)cursor_ + 5, null_pos - (cursor_ + 5));
                    }
                }
                cursor_ += 1 + len;
                current_result_set_done_ = true;
                return false;
            } else if (type == 'Z') { // ReadyForQuery
                current_result_set_done_ = true;
                return false;
            } else {
                cursor_ += 1 + len;
            }
        }
        
        return false;
    }

    bool next_result_set()
    {
        // If the user didn't fetch all rows, skip them and the CommandComplete
        while (!current_result_set_done_ && cursor_ + 5 <= end_) {
            char type = *cursor_;
            uint32_t len;
            memcpy(&len, cursor_ + 1, 4);
            len = ntohl(len);

            if (len < 4 || (size_t)(end_ - cursor_) < 1 + (size_t)len) {
                return false;
            }

            if (type == 'C') {
                if (len >= 5) {
                    const uint8_t* null_pos = (const uint8_t*)memchr(cursor_ + 5, '\0', len - 4);
                    if (null_pos) {
                        command_tag_ = std::string((const char*)cursor_ + 5, null_pos - (cursor_ + 5));
                    }
                }
                cursor_ += 1 + len;
                current_result_set_done_ = true;
                break;
            } else if (type == 'Z') {
                current_result_set_done_ = true;
                break;
            }
            cursor_ += 1 + len;
        }

        // Now we are after 'C'. Check if the next message is 'Z'.
        if (cursor_ < end_ && *cursor_ == 'Z') {
            return false;
        }

        // There might be another result set
        if (cursor_ < end_) {
            fields_.clear();
            command_tag_.clear();
            current_result_set_done_ = false;
            parse_metadata();
            
            if (cursor_ < end_ && *cursor_ == 'Z') {
                return false;
            }
            return true;
        }
        
        return false;
    }

    const std::vector<PostgresField>& get_fields() const { return fields_; }
    const std::string& get_command_tag() const { return command_tag_; }

private:
    void parse_metadata()
    {
        // Parse the initial frames to find RowDescription or skip ParameterDescription/NoData
        while (cursor_ + 5 <= end_) {
            char type = *cursor_;
            uint32_t len;
            memcpy(&len, cursor_ + 1, 4);
            len = ntohl(len);

            if (len < 4 || (size_t)(end_ - cursor_) < 1 + (size_t)len) {
                break; // Malformed frame length or out of bounds
            }

            if (type == 'T') { // RowDescription
                if (len < 6) {
                    cursor_ += 1 + len;
                    continue; // Malformed RowDescription
                }
                int16_t num_fields;
                memcpy(&num_fields, cursor_ + 5, 2);
                num_fields = ntohs(num_fields);

                const uint8_t *field_p = cursor_ + 7;
                const uint8_t *frame_end = cursor_ + 1 + len;
                bool malformed = false;
                for (int i = 0; i < num_fields; i++) {
                    const uint8_t *null_pos = (const uint8_t *)memchr(field_p, '\0', frame_end - field_p);
                    if (!null_pos) { malformed = true; break; }

                    PostgresField field;
                    field.name.assign((const char *)field_p, null_pos - field_p);
                    field_p = null_pos + 1;

                    if ((size_t)(frame_end - field_p) < 18) { malformed = true; break; }

                    memcpy(&field.table_oid, field_p, 4); field.table_oid = ntohl(field.table_oid); field_p += 4;
                    memcpy(&field.column_attr, field_p, 2); field.column_attr = ntohs(field.column_attr); field_p += 2;
                    memcpy(&field.type_oid, field_p, 4); field.type_oid = ntohl(field.type_oid); field_p += 4;
                    memcpy(&field.type_size, field_p, 2); field.type_size = ntohs(field.type_size); field_p += 2;
                    memcpy(&field.type_modifier, field_p, 4); field.type_modifier = ntohl(field.type_modifier); field_p += 4;
                    memcpy(&field.format, field_p, 2); field.format = ntohs(field.format); field_p += 2;

                    fields_.push_back(field);
                }
                
                if (malformed) {
                    fields_.clear();
                }
                cursor_ += 1 + len;
            } else if (type == 't') { // ParameterDescription
                // Parameters are part of the extended query flow. We consume it and advance.
                cursor_ += 1 + len;
            } else if (type == 'n' || type == '1' || type == '2') { // NoData, ParseComplete, BindComplete
                cursor_ += 1 + len;
            } else if (type == 'D' || type == 'C' || type == 'Z' || type == 'E') {
                break; // Hit data, end of command, connection close, or error
            } else {
                // NoticeResponse (N), etc.
                cursor_ += 1 + len;
            }
        }
    }

    const uint8_t *head_;
    const uint8_t *end_;
    const uint8_t *cursor_;
    
    bool current_result_set_done_ = false;

    std::vector<PostgresField> fields_;
    std::string command_tag_;
};

} // namespace protocol

#endif
