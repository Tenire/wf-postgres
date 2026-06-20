#include "PostgresRequest.h"
#include "PostgresWireUtil.h"
#include "PostgresInternal.h"
#include <string.h>
#include <arpa/inet.h>
#include "PostgresWireUtil.h"

namespace wfpg {
namespace protocol {

int PostgresRequest::encode(struct iovec vectors[], int max)
{
    if (is_disconnect_) return encode_terminate(vectors);
    if (is_cancel_) return encode_cancel(vectors);
    if (is_copy_) return encode_copy(vectors);
    if (wait_notification_) return 0;
    if (is_startup_) return encode_startup(vectors);
    if (!has_params_) return encode_simple_query(vectors);
    return encode_extended_query(vectors);
}

int PostgresRequest::append(const void *buf, size_t *size)
{
    buf_.append((const char *)buf, *size);
    return 0;
}

int PostgresRequest::encode_terminate(struct iovec vectors[]) {
    buf_.clear();
    buf_.push_back('X');
    PostgresWireUtil::write_uint32(buf_, 4);
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresRequest::encode_cancel(struct iovec vectors[]) {
    buf_.clear();
    PostgresWireUtil::write_uint32(buf_, 12 + cancel_secret_data_.size());
    PostgresWireUtil::write_uint32(buf_, 80877102);
    PostgresWireUtil::write_uint32(buf_, cancel_pid_);
    buf_.append(cancel_secret_data_);
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresRequest::encode_copy(struct iovec vectors[]) {
    buf_.clear();
    if (copy_fail_) {
        char type = 'f'; // CopyFail
        std::string payload = copy_data_;
        payload.push_back('\0');
        buf_.append(&type, 1);
        PostgresWireUtil::write_uint32(buf_, 4 + payload.size());
        buf_.append(payload);
    } else {
        if (!copy_data_.empty()) {
            char type = 'd'; // CopyData
            buf_.append(&type, 1);
            PostgresWireUtil::write_uint32(buf_, 4 + copy_data_.size());
            buf_.append(copy_data_);
        }
        if (copy_done_) {
            char type = 'c'; // CopyDone
            buf_.append(&type, 1);
            PostgresWireUtil::write_uint32(buf_, 4);
        }
    }
    if (buf_.empty()) return 0;
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresRequest::encode_startup(struct iovec vectors[]) {
    std::string params;
    params += "user";
    params.push_back('\0');
    params += user_;
    params.push_back('\0');
    
    if (!db_.empty()) {
        params += "database";
        params.push_back('\0');
        params += db_;
        params.push_back('\0');
    }
    
    for (const auto& kv : startup_params_) {
        if (kv.first != "user" && kv.first != "database") {
            params += kv.first;
            params.push_back('\0');
            params += kv.second;
            params.push_back('\0');
        }
    }
    params.push_back('\0');

    buf_.clear();
    PostgresWireUtil::write_uint32(buf_, 4 + 4 + params.size());
    PostgresWireUtil::write_uint32(buf_, protocol_version_);
    buf_.append(params);
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresRequest::encode_simple_query(struct iovec vectors[]) {
    char type = 'Q';
    std::string payload = query_;
    payload.push_back('\0');
    
    buf_.clear();
    buf_.append(&type, 1);
    PostgresWireUtil::write_uint32(buf_, 4 + payload.size());
    buf_.append(payload);
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresRequest::encode_extended_query(struct iovec vectors[]) {
    buf_.clear();
    // 1. Parse 'P'
    if (!skip_parse_) {
        char type_P = 'P';
        std::string parse_payload;
        parse_payload += statement_name_;
        parse_payload.push_back('\0');
        parse_payload += query_;
        parse_payload.push_back('\0');
        PostgresWireUtil::write_uint16(parse_payload, params_.size());
        for (const auto& param : params_) {
            PostgresWireUtil::write_uint32(parse_payload, param.type_oid);
        }
        buf_.append(&type_P, 1);
        PostgresWireUtil::write_uint32(buf_, 4 + parse_payload.size());
        buf_.append(parse_payload);
    }
    
    // 2. Bind 'B'
    char type_B = 'B';
    std::string bind_payload;
    bind_payload += portal_name_; bind_payload.push_back('\0');
    bind_payload += statement_name_; bind_payload.push_back('\0');
    
    // parameter_format_codes
    PostgresWireUtil::write_uint16(bind_payload, params_.size());
    for (const auto& param : params_) {
        PostgresWireUtil::write_uint16(bind_payload, param.format);
    }
    
    // parameters
    PostgresWireUtil::write_uint16(bind_payload, params_.size());
    for (const auto& param : params_) {
        if (param.is_null) {
            PostgresWireUtil::write_int32(bind_payload, -1);
        } else {
            PostgresWireUtil::write_int32(bind_payload, param.data.size());
            bind_payload.append(param.data);
        }
    }
    
    // result_format_codes
    PostgresWireUtil::write_uint16(bind_payload, 1); // 1 format code for all columns
    PostgresWireUtil::write_uint16(bind_payload, result_format_); // 0 = text, 1 = binary
    
    buf_.append(&type_B, 1);
    PostgresWireUtil::write_uint32(buf_, 4 + bind_payload.size());
    buf_.append(bind_payload);
    
    // 3. Describe 'D'
    char type_D = 'D';
    std::string desc_payload;
    desc_payload.push_back('P'); // Describe Portal
    desc_payload += portal_name_; desc_payload.push_back('\0');
    buf_.append(&type_D, 1);
    PostgresWireUtil::write_uint32(buf_, 4 + desc_payload.size());
    buf_.append(desc_payload);
    
    // 4. Execute 'E'
    char type_E = 'E';
    std::string exec_payload;
    exec_payload += portal_name_; exec_payload.push_back('\0');
    PostgresWireUtil::write_uint32(exec_payload, 0); // 0 = all rows
    buf_.append(&type_E, 1);
    PostgresWireUtil::write_uint32(buf_, 4 + exec_payload.size());
    buf_.append(exec_payload);
    
    // 5. Sync 'S'
    char type_S = 'S';
    buf_.append(&type_S, 1);
    PostgresWireUtil::write_uint32(buf_, 4);
    
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresSSLRequest::encode(struct iovec vectors[], int max)
{
    // SSLRequest packet: length=8, code=80877103
    buf_.clear();
    PostgresWireUtil::write_uint32(buf_, 8);
    PostgresWireUtil::write_uint32(buf_, 80877103);

    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

} // namespace protocol
} // namespace wfpg
