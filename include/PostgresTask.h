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

    /* Equivalent to WFPostgresConnection::create_disconnect_task() when the
     provided URL/ParsedURI carries the same routing key.
     For PostgreSQL this sends a protocol Terminate ('X') packet and sets
     keep_alive=0. It is the public factory-level equivalent of MySQL/Coke's
     empty-query + keep_alive=0 disconnect pattern.
    */
    static WFPostgresTask *create_disconnect_task(const std::string& url,
                                                  int retry_max,
                                                  postgres_callback_t callback);

    // Create a disconnect task using a parsed URI.
    static WFPostgresTask *create_disconnect_task(const ParsedURI& uri,
                                                  int retry_max,
                                                  postgres_callback_t callback);
};

} // namespace wfpg

#endif
