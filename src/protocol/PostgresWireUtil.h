#ifndef _WF_POSTGRES_WIRE_UTIL_H_
#define _WF_POSTGRES_WIRE_UTIL_H_

#include <stdint.h>
#include <string>
#include <arpa/inet.h>
#include <string.h>

namespace wfpg {
namespace protocol {

class PostgresWireUtil {
public:
    static inline uint16_t read_uint16(const uint8_t* data) {
        uint16_t v;
        memcpy(&v, data, 2);
        return ntohs(v);
    }

    static inline uint32_t read_uint32(const uint8_t* data) {
        uint32_t v;
        memcpy(&v, data, 4);
        return ntohl(v);
    }

    static inline uint64_t read_uint64(const uint8_t* data) {
        uint32_t high = read_uint32(data);
        uint32_t low = read_uint32(data + 4);
        return ((uint64_t)high << 32) | low;
    }

    static inline int16_t read_int16(const uint8_t* data) {
        return (int16_t)read_uint16(data);
    }

    static inline int32_t read_int32(const uint8_t* data) {
        return (int32_t)read_uint32(data);
    }

    static inline int64_t read_int64(const uint8_t* data) {
        return (int64_t)read_uint64(data);
    }

    static inline void write_uint16(std::string& buf, uint16_t v) {
        v = htons(v);
        buf.append((const char*)&v, 2);
    }

    static inline void write_uint32(std::string& buf, uint32_t v) {
        v = htonl(v);
        buf.append((const char*)&v, 4);
    }

    static inline void write_uint64(std::string& buf, uint64_t v) {
        write_uint32(buf, (uint32_t)(v >> 32));
        write_uint32(buf, (uint32_t)(v & 0xFFFFFFFF));
    }

    static inline void write_int16(std::string& buf, int16_t v) {
        write_uint16(buf, (uint16_t)v);
    }

    static inline void write_int32(std::string& buf, int32_t v) {
        write_uint32(buf, (uint32_t)v);
    }

    static inline void write_int64(std::string& buf, int64_t v) {
        write_uint64(buf, (uint64_t)v);
    }

    static inline void write_string(std::string& buf, const std::string& str) {
        buf.append(str.c_str(), str.length() + 1);
    }

    static inline void write_n(std::string& buf, const char* data, size_t len) {
        buf.append(data, len);
    }

    static inline bool read_cstring(const uint8_t*& p, const uint8_t* end, std::string* out) {
        const uint8_t* null_pos = (const uint8_t*)memchr(p, '\0', end - p);
        if (!null_pos) return false;
        if (out) out->assign((const char*)p, null_pos - p);
        p = null_pos + 1;
        return true;
    }
};

} // namespace protocol
} // namespace wfpg

#endif
