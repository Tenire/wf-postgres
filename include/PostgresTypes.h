#ifndef _WF_POSTGRES_TYPES_H_
#define _WF_POSTGRES_TYPES_H_

#include <string>
#include <stdint.h>

namespace wfpg {
namespace protocol {

struct PostgresParameter {
    bool is_null;
    int format; // 0 = text, 1 = binary
    int32_t type_oid; // 0 = infer
    std::string data;

    PostgresParameter() : is_null(true), format(0), type_oid(0) {}
    PostgresParameter(const std::string& val, int fmt = 0, int32_t oid = 0) 
        : is_null(false), format(fmt), type_oid(oid), data(val) {}
};

struct PostgresError {
    std::string severity;
    std::string sql_state;
    std::string message;
    std::string detail;
    std::string hint;
    std::string position;
};

struct PostgresNotification {
    uint32_t backend_pid;
    std::string channel;
    std::string payload;
};

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

} // namespace protocol
} // namespace wfpg

#endif
