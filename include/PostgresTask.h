#ifndef _WF_POSTGRES_TASK_H_
#define _WF_POSTGRES_TASK_H_

#include "workflow/WFTaskFactory.h"
#include "PostgresMessage.h"
#include <functional>
#include <string>

namespace protocol {

using WFPostgresTask = WFNetworkTask<PostgresRequest, PostgresResponse>;
using postgres_callback_t = std::function<void (WFPostgresTask *)>;

class ComplexPostgresTask : public WFComplexClientTask<PostgresRequest, PostgresResponse>
{
public:
    ComplexPostgresTask(int retry_max, postgres_callback_t&& callback)
        : WFComplexClientTask(retry_max, std::move(callback)) {}

protected:
    virtual bool check_request() override;
    virtual CommMessageOut *message_out() override;
    virtual CommMessageIn *message_in() override;
    virtual int keep_alive_timeout() override;
    virtual int first_timeout() override;
    virtual bool init_success() override;
    virtual bool finish_once() override;
    virtual void handle(int state, int error) override;
    virtual WFConnection *get_connection() const override;

private:
    std::string user_;
    std::string pass_;
    std::string db_;
    bool is_user_request_ = true;
    bool is_ssl_ = false;
};

} // namespace protocol

class WFPostgresTaskFactory
{
public:
    static protocol::WFPostgresTask *create_postgres_task(const std::string& url,
                                                          int retry_max,
                                                          protocol::postgres_callback_t callback);

    static protocol::WFPostgresTask *create_postgres_task(const ParsedURI& uri,
                                                          int retry_max,
                                                          protocol::postgres_callback_t callback);
};

#endif
