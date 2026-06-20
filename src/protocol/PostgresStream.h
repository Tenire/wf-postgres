#ifndef _WF_POSTGRES_STREAM_H_
#define _WF_POSTGRES_STREAM_H_

#include <stdint.h>
#include <stddef.h>
#include <string>

namespace wfpg {
namespace protocol {

struct PostgresFrame {
    char type;
    const uint8_t *payload;
    size_t payload_size; // Length of payload (frame length - 4)
    size_t frame_size;   // Total length of frame (type + length + payload), or (length + payload) for startup
};

class PostgresStream {
public:
    PostgresStream(std::string& buf, size_t& cursor) 
        : buf_(&buf), cursor_(cursor), raw_buf_(nullptr), raw_size_(0) {}
    PostgresStream(const std::string& buf, size_t& cursor) 
        : buf_(nullptr), cursor_(cursor), raw_buf_(buf.c_str()), raw_size_(buf.size()) {}
    PostgresStream(const void* buf, size_t size, size_t& cursor) 
        : buf_(nullptr), cursor_(cursor), raw_buf_((const char*)buf), raw_size_(size) {}
    
    ~PostgresStream() {}

    void append(const void *data, size_t size);
    bool next_frame(PostgresFrame *frame, bool is_startup = false);
    size_t consumed_size() const { return cursor_; }
    size_t buffered_size() const { 
        if (buf_) return buf_->size() - cursor_;
        return raw_size_ - cursor_;
    }
    
    const void* get_buf() const { 
        if (buf_) return buf_->c_str();
        return raw_buf_;
    }
    size_t get_buf_size() const { 
        if (buf_) return buf_->size();
        return raw_size_;
    }

    void compact();
    void truncate_unconsumed() { 
        if (buf_ && buf_->size() > cursor_) buf_->resize(cursor_); 
    }
    void clear();

private:
    std::string* buf_;
    size_t& cursor_;
    const char* raw_buf_;
    size_t raw_size_;
};

} // namespace protocol
} // namespace wfpg

#endif
