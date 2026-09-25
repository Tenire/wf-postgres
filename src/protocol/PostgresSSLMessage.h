#ifndef _WF_POSTGRES_SSL_MESSAGE_H_
#define _WF_POSTGRES_SSL_MESSAGE_H_

#include "PostgresRequest.h"

#include "PostgresTask.h"

namespace wfpg {
namespace protocol {

// Internal SSL Startup Request for STARTTLS
class PostgresSSLRequest : public PostgresRequest
{
protected:
    virtual int encode(struct iovec vectors[], int max) override;
};

// Internal SSL Startup Response for STARTTLS
class PostgresSSLResponse : public ::protocol::ProtocolMessage
{
public:
    char get_byte() const { return byte_; }
protected:
    virtual int append(const void *buf, size_t *size) override;
private:
    char byte_ = 0;
};

void __postgres_task_set_ssl_ctx(WFPostgresTask *task, void *ssl_ctx);

} // namespace protocol
} // namespace wfpg

#endif // _WF_POSTGRES_SSL_MESSAGE_H_
