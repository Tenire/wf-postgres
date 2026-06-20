#include "PostgresStream.h"
#include "PostgresWireUtil.h"

namespace wfpg {
namespace protocol {

void PostgresStream::append(const void *data, size_t size) {
    if (buf_) buf_->append((const char *)data, size);
}

bool PostgresStream::next_frame(PostgresFrame *frame, bool is_startup) {
    size_t available = buffered_size();
    const char* buf_ptr = (const char*)get_buf();
    
    if (is_startup) {
        if (available < 4) return false;
        uint32_t len = PostgresWireUtil::read_uint32((const uint8_t*)buf_ptr + cursor_);
        if (available < len) return false;
        
        frame->type = 0;
        frame->payload = (const uint8_t*)buf_ptr + cursor_ + 4;
        frame->payload_size = len - 4;
        frame->frame_size = len;
        
        cursor_ += len;
        return true;
    }

    if (available < 5) return false;

    char type = buf_ptr[cursor_];
    uint32_t len = PostgresWireUtil::read_uint32((const uint8_t*)buf_ptr + cursor_ + 1);

    if (len < 4) {
        // Invalid length, but we should safely return it as a malformed frame 
        // to let the parser or response object handle the error.
        frame->type = type;
        frame->payload = nullptr;
        frame->payload_size = 0;
        frame->frame_size = 5;
        cursor_ += 5;
        return true;
    }

    if (available < 1 + len) return false;

    frame->type = type;
    frame->payload = (const uint8_t*)buf_ptr + cursor_ + 5;
    frame->payload_size = len - 4;
    frame->frame_size = 1 + len;

    cursor_ += 1 + len;
    return true;
}

void PostgresStream::compact() {
    if (cursor_ > 0) {
        if (buf_) buf_->erase(0, cursor_);
        else if (raw_buf_) {
            raw_buf_ += cursor_;
            raw_size_ -= cursor_;
        }
        cursor_ = 0;
    }
}

void PostgresStream::clear() {
    if (buf_) buf_->clear();
    else if (raw_buf_) {
        raw_buf_ = nullptr;
        raw_size_ = 0;
    }
    cursor_ = 0;
}

} // namespace protocol
} // namespace wfpg
