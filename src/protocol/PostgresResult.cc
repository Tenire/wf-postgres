#include "PostgresResult.h"
#include "PostgresResponse.h"
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <endian.h>
#include "PostgresWireUtil.h"
#include "PostgresStream.h"
#include "PostgresParser.h"
#include <string.h>

namespace wfpg {
namespace protocol {

bool PostgresCell::as_bool() const {
    if (is_null_ || length_ == 0) return false;
    if (field_ && field_->format == 1) {
        return *((const char*)data_) != 0;
    }
    char c = *((const char*)data_);
    return c == 't' || c == 'T' || c == '1' || c == 'y' || c == 'Y';
}

long long PostgresCell::as_int() const {
    if (is_null_ || length_ == 0) return 0;
    if (field_ && field_->format == 1) { // binary
        if (length_ == 2) {
            return PostgresWireUtil::read_uint16((const uint8_t*)data_);
        } else if (length_ == 4) {
            return (int32_t)PostgresWireUtil::read_uint32((const uint8_t*)data_);
        } else if (length_ == 8) {
            return (int64_t)PostgresWireUtil::read_uint64((const uint8_t*)data_);
        }
        errno = EINVAL;
        return 0; // fallback
    }
    std::string tmp((const char*)data_, length_);
    char* end;
    long long val = strtoll(tmp.c_str(), &end, 10);
    if (*end != '\0') errno = EINVAL;
    return val;
}

int64_t PostgresCell::as_bigint() const {
    if (is_null_) return 0;
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return 0; }
        return (int64_t)PostgresWireUtil::read_uint64((const uint8_t*)data_);
    }
    std::string tmp((const char*)data_, length_);
    char* end;
    long long val = strtoll(tmp.c_str(), &end, 10);
    if (*end != '\0') errno = EINVAL;
    return val;
}

double PostgresCell::as_double() const {
    if (is_null_) return 0.0;
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return 0.0; }
        uint64_t v = PostgresWireUtil::read_uint64((const uint8_t*)data_);
        double d;
        memcpy(&d, &v, sizeof(double));
        return d;
    }
    std::string tmp((const char*)data_, length_);
    char* end;
    double val = strtod(tmp.c_str(), &end);
    if (*end != '\0') errno = EINVAL;
    return val;
}

float PostgresCell::as_float() const {
    if (is_null_) return 0.0f;
    if (field_ && field_->format == 1) {
        if (length_ != 4) { errno = EINVAL; return 0.0f; }
        uint32_t v = PostgresWireUtil::read_uint32((const uint8_t*)data_);
        float f;
        memcpy(&f, &v, sizeof(float));
        return f;
    }
    std::string tmp((const char*)data_, length_);
    char* end;
    float val = strtof(tmp.c_str(), &end);
    if (*end != '\0') errno = EINVAL;
    return val;
}

std::string PostgresCell::as_datetime_string() const {
    if (is_null_) return "";
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return ""; }
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
    return std::string((const char*)data_, length_);
}

std::string PostgresCell::as_date_string() const {
    if (is_null_) return "";
    if (field_ && field_->format == 1) {
        if (length_ != 4) { errno = EINVAL; return ""; }
        int32_t v = as_int();
        time_t epoch = (v * 86400LL) + 946684800LL;
        struct tm t;
        gmtime_r(&epoch, &t);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        return std::string(buf);
    }
    return std::string((const char*)data_, length_);
}

std::string PostgresCell::as_time_string() const {
    if (is_null_) return "";
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return ""; }
        int64_t v = as_bigint(); // microseconds since midnight
        int micros = v % 1000000LL;
        int total_secs = v / 1000000LL;
        int hours = total_secs / 3600;
        int mins = (total_secs % 3600) / 60;
        int secs = total_secs % 60;
        char buf[32];
        if (micros > 0) {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06d", hours, mins, secs, micros);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, mins, secs);
        }
        return std::string(buf);
    }
    return std::string((const char*)data_, length_);
}

bool PostgresCell::as_date(struct tm *tm) const {
    if (is_null_ || !tm) { errno = EINVAL; return false; }
    memset(tm, 0, sizeof(struct tm));
    if (field_ && field_->format == 1) {
        if (length_ != 4) { errno = EINVAL; return false; }
        int32_t v = as_int();
        time_t epoch = (v * 86400LL) + 946684800LL;
        gmtime_r(&epoch, tm);
        tm->tm_hour = 0; tm->tm_min = 0; tm->tm_sec = 0;
        return true;
    } else {
        std::string s((const char*)data_, length_);
        char* end = strptime(s.c_str(), "%Y-%m-%d", tm);
        if (end != nullptr && *end == '\0') {
            return true;
        }
        errno = EINVAL;
        return false;
    }
}

bool PostgresCell::as_time(struct tm *tm, int *usec) const {
    if (is_null_ || !tm) { errno = EINVAL; return false; }
    memset(tm, 0, sizeof(struct tm));
    if (usec) *usec = 0;
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return false; }
        int64_t v = as_bigint();
        int micros = v % 1000000LL;
        int total_secs = v / 1000000LL;
        if (micros < 0) { micros += 1000000LL; total_secs -= 1; }
        tm->tm_hour = (total_secs / 3600) % 24;
        if (tm->tm_hour < 0) tm->tm_hour += 24;
        tm->tm_min = (total_secs % 3600) / 60;
        if (tm->tm_min < 0) tm->tm_min += 60;
        tm->tm_sec = total_secs % 60;
        if (tm->tm_sec < 0) tm->tm_sec += 60;
        if (usec) *usec = micros;
        return true;
    } else {
        std::string s((const char*)data_, length_);
        char* end = strptime(s.c_str(), "%H:%M:%S", tm);
        if (end != nullptr) {
            if (*end == '.') {
                end++;
                int count = 0;
                int temp_usec = 0;
                int mult = 100000;
                while (isdigit(*end)) {
                    if (count < 6) {
                        temp_usec += (*end - '0') * mult;
                        mult /= 10;
                    }
                    end++;
                    count++;
                }
                if (count > 0 && count <= 6 && *end == '\0') {
                    if (usec) *usec = temp_usec;
                    return true;
                }
            } else if (*end == '\0') {
                if (usec) *usec = 0;
                return true;
            }
        }
        errno = EINVAL;
        return false;
    }
}

bool PostgresCell::as_datetime(struct tm *tm, int *usec) const {
    if (is_null_ || !tm) { errno = EINVAL; return false; }
    memset(tm, 0, sizeof(struct tm));
    if (usec) *usec = 0;
    if (field_ && field_->format == 1) {
        if (length_ != 8) { errno = EINVAL; return false; }
        int64_t v = as_bigint();
        time_t epoch = (v / 1000000LL) + 946684800LL;
        int micros = v % 1000000LL;
        if (micros < 0) {
            micros += 1000000LL;
            epoch -= 1;
        }
        gmtime_r(&epoch, tm);
        if (usec) *usec = micros;
        return true;
    } else {
        std::string s((const char*)data_, length_);
        char* end = strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", tm);
        if (end != nullptr) {
            if (*end == '.') {
                end++;
                int count = 0;
                int temp_usec = 0;
                int mult = 100000;
                while (isdigit(*end)) {
                    if (count < 6) {
                        temp_usec += (*end - '0') * mult;
                        mult /= 10;
                    }
                    end++;
                    count++;
                }
                if (count > 0 && count <= 6 && *end == '\0') {
                    if (usec) *usec = temp_usec;
                    return true;
                }
            } else if (*end == '\0') {
                if (usec) *usec = 0;
                return true;
            }
        }
        errno = EINVAL;
        return false;
    }
}

std::string PostgresCell::as_string() const {
    if (is_null_) return "";
    return std::string((const char*)data_, length_);
}

std::string PostgresCell::as_jsonb_string() const {
    if (is_null_ || length_ == 0) return "";
    if (field_ && field_->format == 1) {
        if (length_ > 0) {
            uint8_t version = *((const uint8_t*)data_);
            if (version == 1) {
                return std::string((const char*)data_ + 1, length_ - 1);
            }
        }
        return std::string((const char*)data_, length_);
    }
    return std::string((const char*)data_, length_);
}

std::string PostgresCell::as_uuid_string() const {
    if (is_null_) return "";
    if (field_ && field_->format == 1) {
        if (length_ != 16) return "";
        const uint8_t *u = (const uint8_t*)data_;
        char buf[40];
        snprintf(buf, sizeof(buf),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
                 u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        return std::string(buf);
    }
    return std::string((const char*)data_, length_);
}

std::vector<PostgresCell> PostgresCell::as_array() const {
    std::vector<PostgresCell> elements;
    if (is_null_ || length_ < 12) return elements;
    if (field_ && field_->format == 1) {
        const uint8_t* p = (const uint8_t*)data_;
        int32_t ndim = PostgresWireUtil::read_uint32(p);
        int32_t has_null = PostgresWireUtil::read_uint32(p + 4);
        int32_t elem_oid = PostgresWireUtil::read_uint32(p + 8);
        p += 12;

        if (ndim == 0) return elements;

        if (length_ < 12 + 8 * ndim) return elements;
        int32_t dim_size = PostgresWireUtil::read_int32(p);
        int32_t lower_bound = PostgresWireUtil::read_int32(p + 4);
        p += 8 * ndim;

        const uint8_t* end = (const uint8_t*)data_ + length_;
        for (int i = 0; i < dim_size; i++) {
            if (p + 4 > end) break;
            int32_t elem_len = PostgresWireUtil::read_int32(p);
            p += 4;

            if (elem_len == -1) {
                elements.push_back({nullptr, 0, true, field_});
            } else if (elem_len >= 0) {
                if ((size_t)(end - p) < (size_t)elem_len) break;
                elements.push_back({p, (size_t)elem_len, false, field_});
                p += elem_len;
            } else {
                break;
            }
        }
    }
    return elements;
}

PostgresResultCursor::PostgresResultCursor(PostgresResponse *resp) {
    head_ = (const uint8_t *)resp->get_buf();
    end_ = head_ + resp->get_buf_size();
    cursor_ = head_;
    current_result_set_done_ = false;
    parse_metadata();
}

void PostgresResultCursor::parse_metadata() {
    fields_.clear();
    command_tag_.clear();
    current_result_set_done_ = false;

    size_t offset = cursor_ - head_;
    PostgresStream s(head_, end_ - head_, offset);
    PostgresFrame frame;

    while (s.next_frame(&frame)) {
        if (!frame.payload) break;

        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);

        if (event.type == PG_EVENT_ROW_DESCRIPTION) {
            PostgresParser::parse_row_description(event.payload, event.payload_size, &fields_);
            cursor_ = head_ + offset;
            return; 
        } else if (event.type == PG_EVENT_COMMAND_COMPLETE) {
            PostgresParser::parse_command_complete(event.payload, event.payload_size, &command_tag_);
            current_result_set_done_ = true;
            cursor_ = head_ + offset;
            return;
        } else if (event.type == PG_EVENT_DATA_ROW || event.type == PG_EVENT_COPY_DATA) {
            cursor_ = head_ + (offset - frame.frame_size); // backtrack to let fetch_row read it
            return; 
        }
    }
    cursor_ = head_ + offset;
}

bool PostgresResultCursor::fetch_row(std::vector<PostgresCell>& row) {
    if (current_result_set_done_ || cursor_ >= end_) return false;
    
    size_t offset = cursor_ - head_;
    PostgresStream s(head_, end_ - head_, offset);
    PostgresFrame frame;

    while (s.next_frame(&frame)) {
        if (!frame.payload) return false;

        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);

        if (event.type == PG_EVENT_DATA_ROW) {
            row.clear();
            if (PostgresParser::parse_data_row(event.payload, event.payload_size, &row, fields_) == 0) {
                cursor_ = head_ + offset;
                return true;
            } else {
                return false;
            }
        } else if (event.type == PG_EVENT_COPY_DATA) {
            row.clear();
            row.reserve(1);
            row.push_back({event.payload, event.payload_size, false, nullptr});
            cursor_ = head_ + offset;
            return true;
        } else if (event.type == PG_EVENT_COMMAND_COMPLETE) {
            PostgresParser::parse_command_complete(event.payload, event.payload_size, &command_tag_);
            current_result_set_done_ = true;
            cursor_ = head_ + offset;
            return false;
        } else if (event.type == PG_EVENT_READY) {
            current_result_set_done_ = true;
            cursor_ = head_ + offset;
            return false;
        }
    }
    
    cursor_ = head_ + offset;
    return false;
}

bool PostgresResultCursor::fetch_row(std::map<std::string, PostgresCell>& row_map) {
    std::vector<PostgresCell> row_arr;
    if (!fetch_row(row_arr)) return false;
    if (row_arr.size() > fields_.size()) return false;
    row_map.clear();
    for (size_t i = 0; i < row_arr.size(); i++) {
        row_map.insert(std::make_pair(fields_[i].name, row_arr[i]));
    }
    return true;
}

bool PostgresResultCursor::fetch_row(std::unordered_map<std::string, PostgresCell>& row_map) {
    std::vector<PostgresCell> row_arr;
    if (!fetch_row(row_arr)) return false;
    if (row_arr.size() > fields_.size()) return false;
    row_map.clear();
    for (size_t i = 0; i < row_arr.size(); i++) {
        row_map.insert(std::make_pair(fields_[i].name, row_arr[i]));
    }
    return true;
}

bool PostgresResultCursor::fetch_all(std::vector<std::vector<PostgresCell>>& rows) {
    rows.clear();
    std::vector<PostgresCell> row;
    while (fetch_row(row)) {
        rows.push_back(row);
    }
    return !rows.empty();
}

bool PostgresResultCursor::next_result_set() {
    size_t offset = cursor_ - head_;
    PostgresStream s(head_, end_ - head_, offset);
    PostgresFrame frame;

    while (!current_result_set_done_ && s.next_frame(&frame)) {
        if (!frame.payload) return false;
        
        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);

        if (event.type == PG_EVENT_COMMAND_COMPLETE) {
            PostgresParser::parse_command_complete(event.payload, event.payload_size, &command_tag_);
            current_result_set_done_ = true;
            break;
        } else if (event.type == PG_EVENT_READY) {
            current_result_set_done_ = true;
            cursor_ = head_ + offset;
            return false;
        }
    }

    while (s.next_frame(&frame)) {
        if (!frame.payload) return false;

        PostgresBackendEvent event = PostgresParser::parse_backend_event(frame);
        if (event.type == PG_EVENT_READY) {
            cursor_ = head_ + offset;
            return false;
        }
        if (event.type == PG_EVENT_ROW_DESCRIPTION || event.type == PG_EVENT_DATA_ROW || event.type == PG_EVENT_COMMAND_COMPLETE) {
            cursor_ = head_ + (offset - frame.frame_size);
            parse_metadata();
            return true;
        }
    }
    
    cursor_ = head_ + offset;
    return false;
}

int PostgresResultCursor::get_field_count() const {
    return fields_.size();
}

static void parse_command_tag_parts(
    const std::string& tag,
    std::string* command,
    unsigned long long* oid,
    unsigned long long* affected_rows)
{
    if (command) command->clear();
    if (oid) *oid = 0;
    if (affected_rows) *affected_rows = 0;

    if (tag.empty()) return;

    std::vector<std::string> tokens;
    size_t start = 0;
    while (start < tag.length()) {
        while (start < tag.length() && tag[start] == ' ') start++;
        if (start == tag.length()) break;
        size_t end = tag.find(' ', start);
        if (end == std::string::npos) end = tag.length();
        tokens.push_back(tag.substr(start, end - start));
        start = end;
    }

    if (tokens.empty()) return;

    if (command) *command = tokens[0];

    // parse affected_rows (last token if purely numeric)
    if (affected_rows) {
        const std::string& last = tokens.back();
        bool is_num = !last.empty();
        for (char c : last) {
            if (!isdigit(c)) { is_num = false; break; }
        }
        if (is_num) {
            errno = 0;
            char* end_ptr;
            unsigned long long val = strtoull(last.c_str(), &end_ptr, 10);
            if (errno == 0 && *end_ptr == '\0') {
                *affected_rows = val;
            } else {
                errno = EINVAL;
            }
        }
    }

    // parse insert oid (INSERT oid rows)
    if (oid && tokens.size() >= 3 && tokens[0] == "INSERT") {
        const std::string& oid_str = tokens[1];
        bool is_num = !oid_str.empty();
        for (char c : oid_str) {
            if (!isdigit(c)) { is_num = false; break; }
        }
        if (is_num) {
            errno = 0;
            char* end_ptr;
            unsigned long long val = strtoull(oid_str.c_str(), &end_ptr, 10);
            if (errno == 0 && *end_ptr == '\0') {
                *oid = val;
            } else {
                errno = EINVAL;
            }
        }
    }
}

std::string PostgresResultCursor::get_command() const {
    std::string command;
    parse_command_tag_parts(command_tag_, &command, nullptr, nullptr);
    return command;
}

unsigned long long PostgresResultCursor::get_insert_oid() const {
    unsigned long long oid = 0;
    parse_command_tag_parts(command_tag_, nullptr, &oid, nullptr);
    return oid;
}

unsigned long long PostgresResultCursor::get_affected_rows() const {
    unsigned long long affected = 0;
    parse_command_tag_parts(command_tag_, nullptr, nullptr, &affected);
    return affected;
}

} // namespace protocol
} // namespace wfpg
