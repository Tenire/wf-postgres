#ifndef _WF_POSTGRES_RESULT_H_
#define _WF_POSTGRES_RESULT_H_


#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <string.h>

#include "PostgresTypes.h"

namespace wfpg {
namespace protocol {
class PostgresResponse;

// Represents a parsed value. is_null is true if length was -1.
struct PostgresCell {
private:
    const void *data_;
    size_t length_;
    bool is_null_;
    const PostgresField *field_;

    friend class PostgresResultCursor;

public:
    PostgresCell() : data_(nullptr), length_(0), is_null_(true), field_(nullptr) {}
    PostgresCell(const void* data, size_t length, bool is_null, const PostgresField* field)
        : data_(data), length_(length), is_null_(is_null), field_(field) {}

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
};

class PostgresResultCursor
{
public:
    PostgresResultCursor(PostgresResponse *resp);

    // Move to the next row in the current result set. Returns false if no more rows.
    bool fetch_row(std::vector<PostgresCell>& row);
    bool fetch_row(std::map<std::string, PostgresCell>& row_map);
    bool fetch_row(std::unordered_map<std::string, PostgresCell>& row_map);
    bool fetch_all(std::vector<std::vector<PostgresCell>>& rows);

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
};

} // namespace protocol
} // namespace wfpg

#endif // _WF_POSTGRES_RESULT_H_
