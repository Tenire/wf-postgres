#ifndef _WF_POSTGRES_VALUE_H_
#define _WF_POSTGRES_VALUE_H_

#include <string>
#include <vector>
#include <stdint.h>
#include <chrono>
#include <ctime>
#include "PostgresTypes.h"

namespace wfpg {

namespace detail {
    inline std::string to_hex_bytea(const uint8_t* data, size_t len) {
        static const char hex_digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(2 + len * 2);
        s.append("\\x");
        for (size_t i = 0; i < len; ++i) {
            s.push_back(hex_digits[(data[i] >> 4) & 0x0F]);
            s.push_back(hex_digits[data[i] & 0x0F]);
        }
        return s;
    }
} // namespace detail

class PostgresValue {
public:
    // Null value
    PostgresValue() : is_null_(true), type_oid_(0) {}
    PostgresValue(std::nullptr_t) : is_null_(true), type_oid_(0) {}

    // Boolean
    PostgresValue(bool v)
        : is_null_(false), type_oid_(protocol::PostgresOid::BOOL), data_(v ? "t" : "f") {}

    // Integers
    PostgresValue(int16_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT2), data_(std::to_string(v)) {}
    PostgresValue(int32_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT4), data_(std::to_string(v)) {}
    PostgresValue(int64_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT8), data_(std::to_string(v)) {}
    PostgresValue(uint16_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT4), data_(std::to_string(v)) {}
    PostgresValue(uint32_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT8), data_(std::to_string(v)) {}
    PostgresValue(uint64_t v)
        : is_null_(false), type_oid_(protocol::PostgresOid::NUMERIC), data_(std::to_string(v)) {}

    // Floating point
    PostgresValue(float v)
        : is_null_(false), type_oid_(protocol::PostgresOid::FLOAT4), data_(std::to_string(v)) {}
    PostgresValue(double v)
        : is_null_(false), type_oid_(protocol::PostgresOid::FLOAT8), data_(std::to_string(v)) {}

    // String / Text
    PostgresValue(const char* s)
        : is_null_(s == nullptr), type_oid_(protocol::PostgresOid::TEXT), data_(s ? s : "") {}
    PostgresValue(const std::string& s)
        : is_null_(false), type_oid_(protocol::PostgresOid::TEXT), data_(s) {}
    PostgresValue(std::string&& s)
        : is_null_(false), type_oid_(protocol::PostgresOid::TEXT), data_(std::move(s)) {}

    // Bytea (Binary)
    PostgresValue(const std::vector<uint8_t>& bytes)
        : is_null_(false), type_oid_(protocol::PostgresOid::BYTEA),
          data_(detail::to_hex_bytea(bytes.data(), bytes.size())) {}

    static PostgresValue bytea(const void* data, size_t len) {
        PostgresValue v;
        v.is_null_ = (data == nullptr);
        v.type_oid_ = protocol::PostgresOid::BYTEA;
        if (data && len > 0) {
            v.data_ = detail::to_hex_bytea((const uint8_t*)data, len);
        }
        return v;
    }

    // Time & Date
    PostgresValue(const struct tm& tm_val, int32_t type_oid = protocol::PostgresOid::TIMESTAMP)
        : is_null_(false), type_oid_(type_oid) {
        char buf[64];
        if (type_oid == protocol::PostgresOid::DATE) {
            strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
        } else if (type_oid == protocol::PostgresOid::TIME) {
            strftime(buf, sizeof(buf), "%H:%M:%S", &tm_val);
        } else {
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
        }
        data_ = buf;
    }

    PostgresValue(const std::chrono::system_clock::time_point& tp)
        : is_null_(false), type_oid_(protocol::PostgresOid::TIMESTAMPTZ) {
        auto duration = tp.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration - seconds);

        int64_t sec_count = seconds.count();
        int64_t usec_count = micros.count();
        if (usec_count < 0) {
            usec_count += 1000000;
            --sec_count;
        }

        std::time_t tt = (std::time_t)sec_count;
        struct tm tm_val;
        gmtime_r(&tt, &tm_val);

        char full_buf[128];
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
        snprintf(full_buf, sizeof(full_buf), "%s.%06d+00", buf, (int)usec_count);
        data_ = full_buf;
    }

    // Numeric / High Precision
    static PostgresValue numeric(const std::string& num_str) {
        return PostgresValue(num_str, protocol::PostgresOid::NUMERIC);
    }

    // JSON / JSONB
    static PostgresValue json(const std::string& json_str) {
        return PostgresValue(json_str, protocol::PostgresOid::JSON);
    }

    static PostgresValue jsonb(const std::string& json_str) {
        return PostgresValue(json_str, protocol::PostgresOid::JSONB);
    }

    // UUID
    static PostgresValue uuid(const std::string& uuid_str) {
        return PostgresValue(uuid_str, protocol::PostgresOid::UUID);
    }

    // Custom OID constructor
    PostgresValue(const std::string& s, int32_t type_oid)
        : is_null_(false), type_oid_(type_oid), data_(s) {}

    // Array constructors for common types
    PostgresValue(const std::vector<int32_t>& vec)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT4_ARRAY) {
        data_ = format_array(vec);
    }

    PostgresValue(const std::vector<int64_t>& vec)
        : is_null_(false), type_oid_(protocol::PostgresOid::INT8_ARRAY) {
        data_ = format_array(vec);
    }

    PostgresValue(const std::vector<double>& vec)
        : is_null_(false), type_oid_(protocol::PostgresOid::FLOAT8_ARRAY) {
        data_ = format_array(vec);
    }

    PostgresValue(const std::vector<std::string>& vec)
        : is_null_(false), type_oid_(protocol::PostgresOid::TEXT_ARRAY) {
        std::string s = "{";
        for (size_t i = 0; i < vec.size(); ++i) {
            if (i > 0) s.push_back(',');
            s.push_back('"');
            for (char c : vec[i]) {
                if (c == '"' || c == '\\') s.push_back('\\');
                s.push_back(c);
            }
            s.push_back('"');
        }
        s.push_back('}');
        data_ = std::move(s);
    }

    static PostgresValue null(int32_t type_oid = 0) {
        PostgresValue v;
        v.type_oid_ = type_oid;
        return v;
    }

    bool is_null() const { return is_null_; }
    int32_t type_oid() const { return type_oid_; }
    const std::string& data() const { return data_; }

    protocol::PostgresParameter to_parameter() const {
        if (is_null_) {
            protocol::PostgresParameter p;
            p.is_null = true;
            p.type_oid = type_oid_;
            p.format = 0;
            return p;
        }
        return protocol::PostgresParameter(data_, 0, type_oid_);
    }

private:
    template <typename T>
    static std::string format_array(const std::vector<T>& vec) {
        std::string s = "{";
        for (size_t i = 0; i < vec.size(); ++i) {
            if (i > 0) s.push_back(',');
            s += std::to_string(vec[i]);
        }
        s.push_back('}');
        return s;
    }

    bool is_null_{true};
    int32_t type_oid_{0};
    std::string data_;
};

namespace detail {
    inline void collect_params(std::vector<protocol::PostgresParameter>&) {}

    template <typename T, typename... Args>
    inline void collect_params(std::vector<protocol::PostgresParameter>& out, const T& first, const Args&... rest) {
        out.push_back(PostgresValue(first).to_parameter());
        collect_params(out, rest...);
    }
} // namespace detail

template <typename... Args>
inline std::vector<protocol::PostgresParameter> bind_params(const Args&... args) {
    std::vector<protocol::PostgresParameter> params;
    params.reserve(sizeof...(args));
    detail::collect_params(params, args...);
    return params;
}

} // namespace wfpg

#endif // _WF_POSTGRES_VALUE_H_
