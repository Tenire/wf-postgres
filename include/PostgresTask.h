#ifndef _WF_POSTGRES_TASK_H_
#define _WF_POSTGRES_TASK_H_

#include "workflow/WFTask.h"
#include "workflow/URIParser.h"
#include "PostgresRequest.h"
#include "PostgresResponse.h"
#include <functional>
#include <string>

namespace wfpg {

using WFPostgresTask = WFNetworkTask<protocol::PostgresRequest, protocol::PostgresResponse>;
using postgres_callback_t = std::function<void (WFPostgresTask *)>;

// ComplexPostgresTask is implemented in src/factory/PostgresTaskImpl.cc.

class WFPostgresTaskFactory
{
public:
    static WFPostgresTask *create_postgres_task(const std::string& url,
                                                          int retry_max,
                                                          postgres_callback_t callback);

    static WFPostgresTask *create_postgres_task(const ParsedURI& uri,
                                                          int retry_max,
                                                          postgres_callback_t callback);

    static WFPostgresTask *create_cancel_task(const std::string& url,
                                                        int32_t pid,
                                                        const std::string& secret_data,
                                                        int retry_max,
                                                        postgres_callback_t callback);
};

} // namespace wfpg

#endif
