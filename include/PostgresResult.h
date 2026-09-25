#ifndef _WF_POSTGRES_RESULT_H_
#define _WF_POSTGRES_RESULT_H_


#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <string.h>

#include "PostgresTypes.h"
#include "PostgresResponse.h"
namespace wfpg {
namespace protocol {
class PostgresResponse;

// Represents a parsed value. Supports both zero-copy references and owned copies.
struct PostgresCell {
private:
    const void *data_;
    size_t length_;
    bool is_null_;
    const PostgresField *field_;
    std::string storage_; // Owns buffer if this cell was detached or deep-copied

    friend class PostgresResultCursor;

public:
    PostgresCell() : data_(nullptr), length_(0), is_null_(true), field_(nullptr) {}
    PostgresCell(const void* data, size_t length, bool is_null, const PostgresField* field)
        : data_(data), length_(length), is_null_(is_null), field_(field) {}

    PostgresCell(const PostgresCell& other)
        : data_(other.data_), length_(other.length_), is_null_(other.is_null_),
          field_(other.field_), storage_(other.storage_) {
        if (!storage_.empty()) {
            data_ = storage_.data();
        }
    }

    PostgresCell& operator=(const PostgresCell& other) {
        if (this != &other) {
            is_null_ = other.is_null_;
            length_ = other.length_;
            field_ = other.field_;
            storage_ = other.storage_;
            if (!storage_.empty()) {
                data_ = storage_.data();
            } else {
                data_ = other.data_;
            }
        }
        return *this;
    }

    PostgresCell(PostgresCell&& other) noexcept
        : data_(other.data_), length_(other.length_), is_null_(other.is_null_),
          field_(other.field_), storage_(std::move(other.storage_)) {
        if (!storage_.empty()) {
            data_ = storage_.data();
        }
        other.data_ = nullptr;
        other.length_ = 0;
        other.is_null_ = true;
    }

    PostgresCell& operator=(PostgresCell&& other) noexcept {
        if (this != &other) {
            is_null_ = other.is_null_;
            length_ = other.length_;
            field_ = other.field_;
            storage_ = std::move(other.storage_);
            if (!storage_.empty()) {
                data_ = storage_.data();
            } else {
                data_ = other.data_;
            }
            other.data_ = nullptr;
            other.length_ = 0;
            other.is_null_ = true;
        }
        return *this;
    }

    // Detach from response buffer: deep-copy into storage_ so it safely outlives the task
    void detach() {
        if (!is_null_ && data_ && storage_.empty() && length_ > 0) {
            storage_.assign((const char*)data_, length_);
            data_ = storage_.data();
        }
    }

    bool is_detached() const { return !storage_.empty(); }

    // Accessors
    const void* data() const { return data_; }
    size_t length() const { return length_; }
    bool is_null() const { return is_null_; }
    const PostgresField* field() const { return field_; }

    // Type Extraction Methods
    bool as_bool() const;
    long long as_int() const;
    int64_t as_bigint() const;
    double as_double() const;
    float as_float() const;
    std::string as_datetime_string() const;
    std::string as_date_string() const;
    std::string as_time_string() const;
    bool as_date(struct tm *tm) const;
    bool as_time(struct tm *tm, int *usec = nullptr) const;
    bool as_datetime(struct tm *tm, int *usec = nullptr) const;
    std::string as_string() const;
    std::string as_jsonb_string() const;
    std::string as_uuid_string() const;
    std::vector<PostgresCell> as_array() const;
    std::vector<uint8_t> as_bytea() const;
    std::vector<std::string> as_string_array() const;
    std::vector<int64_t> as_bigint_array() const;
};

class PostgresResultCursor
{
public:
    PostgresResultCursor(PostgresResponse *resp);
    PostgresResultCursor(PostgresResponse&& resp);

    PostgresResultCursor(PostgresResultCursor&& other) noexcept;
    PostgresResultCursor& operator=(PostgresResultCursor&& other) noexcept;

    PostgresResultCursor(const PostgresResultCursor&) = delete;
    PostgresResultCursor& operator=(const PostgresResultCursor&) = delete;

    // Move to the next row in the current result set. Returns false if no more rows.
    // Zero-copy by default (points directly to PostgresResponse buffer).
    bool fetch_row(std::vector<PostgresCell>& row);
    bool fetch_row(std::map<std::string, PostgresCell>& row_map);
    bool fetch_row(std::unordered_map<std::string, PostgresCell>& row_map);
    bool fetch_all(std::vector<std::vector<PostgresCell>>& rows);

    // Owned copy variants: cells are detached and safe across coroutine resumptions and task destruction.
    bool fetch_row_copy(std::vector<PostgresCell>& row);
    bool fetch_row_copy(std::map<std::string, PostgresCell>& row_map);
    bool fetch_row_copy(std::unordered_map<std::string, PostgresCell>& row_map);
    bool fetch_all_copy(std::vector<std::vector<PostgresCell>>& rows);
    bool next_result_set();
    int get_field_count() const;
    const std::vector<PostgresField>& get_fields() const { return fields_; }
    const std::string& get_command_tag() const { return command_tag_; }
    std::string get_command() const;
    unsigned long long get_insert_oid() const;
    unsigned long long get_affected_rows() const;

private:
    void parse_metadata();

    const uint8_t *head_;
    const uint8_t *end_;
    const uint8_t *cursor_;
    
    std::vector<PostgresField> fields_;
    std::string command_tag_;
    bool current_result_set_done_;
    bool owns_resp_{false};
    PostgresResponse owned_resp_;
};

} // namespace protocol


} // namespace wfpg

#endif // _WF_POSTGRES_RESULT_H_
