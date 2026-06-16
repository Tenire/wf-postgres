#include "PostgresMessage.h"
#include "ScramAuth.h"
#include <string.h>
#include <arpa/inet.h>

namespace protocol {

int PostgresMessage::encode(struct iovec vectors[], int max)
{
    return 0;
}

int PostgresMessage::append(const void *buf, size_t *size)
{
    buf_.append((const char *)buf, *size);
    return 0;
}

int PostgresRequest::encode(struct iovec vectors[], int max)
{
    if (is_disconnect_) {
        buf_.clear();
        buf_.push_back('X');
        uint32_t len = htonl(4);
        buf_.append((char*)&len, 4);
        vectors[0].iov_base = (void *)buf_.c_str();
        vectors[0].iov_len = buf_.size();
        return 1;
    }

    std::string packet;
    if (is_startup_) {
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
        
        params += "client_encoding";
        params.push_back('\0');
        params += "UTF8";
        params.push_back('\0');
        params.push_back('\0');

        uint32_t len = htonl(4 + 4 + params.size());
        uint32_t proto = htonl(196610); // 3.2 (Major 3, Minor 2)

        packet.append((char*)&len, 4);
        packet.append((char*)&proto, 4);
        packet.append(params);
    } else {
        if (!has_params_) {
            // Only Q (Simple Query) for now
            char type = 'Q';
            std::string payload = query_;
            payload.push_back('\0');
            
            uint32_t len = htonl(4 + payload.size());
            packet.append(&type, 1);
            packet.append((char*)&len, 4);
            packet.append(payload);
        } else {
            // Extended Query Protocol
            
            // 1. Parse 'P'
            char type_P = 'P';
            std::string parse_payload;
            parse_payload.push_back('\0'); // statement=""
            parse_payload += query_;
            parse_payload.push_back('\0');
            uint16_t num_params = htons(0); // 0 types specified, let backend infer
            parse_payload.append((char*)&num_params, 2);
            uint32_t parse_len = htonl(4 + parse_payload.size());
            packet.append(&type_P, 1);
            packet.append((char*)&parse_len, 4);
            packet.append(parse_payload);
            
            // 2. Bind 'B'
            char type_B = 'B';
            std::string bind_payload;
            bind_payload.push_back('\0'); // portal=""
            bind_payload.push_back('\0'); // statement=""
            
            // parameter_format_codes
            uint16_t num_param_formats = htons(0); // all text
            bind_payload.append((char*)&num_param_formats, 2);
            
            // parameters
            uint16_t n_params = htons(params_.size());
            bind_payload.append((char*)&n_params, 2);
            for (const auto& param : params_) {
                uint32_t param_len = htonl(param.size());
                bind_payload.append((char*)&param_len, 4);
                bind_payload.append(param);
            }
            
            // result_format_codes
            uint16_t num_result_formats = htons(1); // 1 format code for all columns
            bind_payload.append((char*)&num_result_formats, 2);
            uint16_t result_format = htons(result_format_); // 0 = text, 1 = binary
            bind_payload.append((char*)&result_format, 2);
            
            uint32_t bind_len = htonl(4 + bind_payload.size());
            packet.append(&type_B, 1);
            packet.append((char*)&bind_len, 4);
            packet.append(bind_payload);
            
            // 3. Describe 'D'
            char type_D = 'D';
            std::string desc_payload;
            desc_payload.push_back('P'); // Describe Portal
            desc_payload.push_back('\0'); // portal=""
            uint32_t desc_len = htonl(4 + desc_payload.size());
            packet.append(&type_D, 1);
            packet.append((char*)&desc_len, 4);
            packet.append(desc_payload);
            
            // 4. Execute 'E'
            char type_E = 'E';
            std::string exec_payload;
            exec_payload.push_back('\0'); // portal=""
            uint32_t max_rows = htonl(0); // 0 = all rows
            exec_payload.append((char*)&max_rows, 4);
            uint32_t exec_len = htonl(4 + exec_payload.size());
            packet.append(&type_E, 1);
            packet.append((char*)&exec_len, 4);
            packet.append(exec_payload);
            
            // 5. Sync 'S'
            char type_S = 'S';
            uint32_t sync_len = htonl(4);
            packet.append(&type_S, 1);
            packet.append((char*)&sync_len, 4);
        }
    }
    
    buf_ = packet;
    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresResponse::append(const void *buf, size_t *size)
{
    const char *p = (const char *)buf;
    size_t len = *size;
    buf_.append(p, len);
    
    bool finished = false;
    
    while (cursor_ + 5 <= buf_.size()) {
        char type = buf_[cursor_];
        uint32_t frame_len;
        memcpy(&frame_len, buf_.c_str() + cursor_ + 1, 4);
        frame_len = ntohl(frame_len);

        if (frame_len < 4) {
            is_error_ = true;
            error_.severity = "FATAL";
            error_.message = "Invalid frame length < 4";
            finished = true;
            break;
        }
        
        if (cursor_ + 1 + frame_len > buf_.size()) {
            break; 
        }

        if (type == 'E') {
            is_error_ = true;
            const uint8_t *p = (const uint8_t *)buf_.c_str() + cursor_ + 5;
            const uint8_t *end = (const uint8_t *)buf_.c_str() + cursor_ + 1 + frame_len;
            while (p < end && *p != '\0') {
                char field_type = *p++;
                const uint8_t *null_pos = (const uint8_t *)memchr(p, '\0', end - p);
                if (!null_pos) break; // Malformed ErrorResponse, no null terminator

                std::string value((const char *)p, null_pos - p);
                p = null_pos + 1;
                switch (field_type) {
                    case 'S': error_.severity = value; break;
                    case 'V': /* Severity (non-localized) */ if (error_.severity.empty()) error_.severity = value; break;
                    case 'C': error_.sql_state = value; break;
                    case 'M': error_.message = value; break;
                    case 'D': error_.detail = value; break;
                    case 'H': error_.hint = value; break;
                    case 'P': error_.position = value; break;
                }
            }
            // If error occurs during startup phase, it's fatal and no Z will follow
            if (is_startup_) {
                finished = true;
                cursor_ += 1 + frame_len;
                break;
            }
        }

        if (type == 'R') {
            if (frame_len < 8) {
                is_error_ = true;
                error_.severity = "FATAL";
                error_.message = "Authentication frame too short (< 8 bytes)";
                finished = true;
                break;
            }

            uint32_t auth_type;
            memcpy(&auth_type, buf_.c_str() + cursor_ + 5, 4);
            auth_type = ntohl(auth_type);

            if (auth_type == 10) { // AuthenticationSASL
                bool supports_scram_sha_256 = false;
                const uint8_t *p = (const uint8_t *)buf_.c_str() + cursor_ + 9;
                const uint8_t *end = (const uint8_t *)buf_.c_str() + cursor_ + 1 + frame_len;
                while (p < end && *p != '\0') {
                    const uint8_t *null_pos = (const uint8_t *)memchr(p, '\0', end - p);
                    if (!null_pos) break; // malformed mechanism list
                    std::string mech((const char*)p, null_pos - p);
                    if (mech == "SCRAM-SHA-256") supports_scram_sha_256 = true;
                    p = null_pos + 1;
                }

                if (!supports_scram_sha_256) {
                    is_error_ = true;
                    error_.severity = "FATAL";
                    error_.message = "Server does not support SCRAM-SHA-256";
                    finished = true;
                    break;
                }

                std::string mechanism = "SCRAM-SHA-256";
                std::string client_first_message_bare = scram_auth_.generate_client_first_message();
                
                if (client_first_message_bare.empty()) {
                    is_error_ = true;
                    error_.severity = "FATAL";
                    error_.message = "Failed to generate SCRAM client first message (RNG failure)";
                    finished = true;
                    break;
                }

                std::string client_first_message_full = "n,," + client_first_message_bare;

                std::string payload = mechanism;
                payload.push_back('\0');
                uint32_t sasl_len = htonl(client_first_message_full.size());
                payload.append((char*)&sasl_len, 4);
                payload += client_first_message_full;

                uint32_t msg_len = htonl(4 + payload.size());
                
                std::string feedback_msg;
                feedback_msg.append("p", 1);
                feedback_msg.append((char*)&msg_len, 4);
                feedback_msg += payload;

                this->feedback(feedback_msg.c_str(), feedback_msg.size());
            } else if (auth_type == 11) { // AuthenticationSASLContinue
                std::string server_first_message = buf_.substr(cursor_ + 9, frame_len - 8);
                std::string client_final_message = scram_auth_.generate_client_final_message(server_first_message);

                if (client_final_message.empty()) {
                    is_error_ = true;
                    error_.severity = "FATAL";
                    error_.message = "SCRAM client final message generation failed (invalid server nonce or PBKDF2 error)";
                    finished = true;
                    break;
                }

                std::string payload = client_final_message;
                uint32_t msg_len = htonl(4 + payload.size());
                
                std::string feedback_msg;
                feedback_msg.append("p", 1);
                feedback_msg.append((char*)&msg_len, 4);
                feedback_msg += payload;

                this->feedback(feedback_msg.c_str(), feedback_msg.size());
            } else if (auth_type == 12) { // AuthenticationSASLFinal
                std::string server_final_message = buf_.substr(cursor_ + 9, frame_len - 8);
                if (!scram_auth_.verify_server_signature(server_final_message)) {
                    is_error_ = true;
                    error_.severity = "FATAL";
                    error_.message = "SCRAM server signature verification failed";
                    finished = true;
                }
            } else if (auth_type == 3) { // CleartextPassword
                std::string payload = pass_;
                payload.push_back('\0');
                uint32_t msg_len = htonl(4 + payload.size());
                std::string feedback_msg;
                feedback_msg.append("p", 1);
                feedback_msg.append((char*)&msg_len, 4);
                feedback_msg += payload;
                this->feedback(feedback_msg.c_str(), feedback_msg.size());
            }
        }
        
        cursor_ += 1 + frame_len;
        
        if (type == 'Z') {
            finished = true;
            break;
        }
    }
    
    if (finished) {
        size_t over_read = buf_.size() - cursor_;
        *size = len - over_read;
        buf_.resize(cursor_); // Remove garbage so cursor doesn't parse it
        return 1; 
    }
    
    *size = len;
    return 0;
}

int PostgresSSLRequest::encode(struct iovec vectors[], int max)
{
    // SSLRequest packet: length=8, code=80877103
    uint32_t len = htonl(8);
    uint32_t code = htonl(80877103);
    buf_.clear();
    buf_.append((char*)&len, 4);
    buf_.append((char*)&code, 4);

    vectors[0].iov_base = (void *)buf_.c_str();
    vectors[0].iov_len = buf_.size();
    return 1;
}

int PostgresSSLResponse::append(const void *buf, size_t *size)
{
    if (*size > 0) {
        byte_ = ((const char *)buf)[0];
        *size = 1; // Consume exactly 1 byte
        return 1;  // Done
    }
    return 0;
}

} // namespace protocol
