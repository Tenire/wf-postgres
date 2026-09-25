#ifndef _WF_POSTGRES_CLIENT_H_
#define _WF_POSTGRES_CLIENT_H_

/**
 * Top-level convenience header for the PostgreSQL Client Plugin for Workflow.
 * Mirrors official Workflow client interfaces (e.g. WFMySQLClient / WFPostgresClient).
 */

#include "PostgresTask.h"
#include "WFPostgresConnection.h"
#include "PostgresRequest.h"
#include "PostgresResponse.h"
#include "PostgresResult.h"
#include "PostgresStatus.h"
#include "PostgresValue.h"

using PostgresResultCursor = wfpg::protocol::PostgresResultCursor;
using PostgresCell = wfpg::protocol::PostgresCell;
using PostgresParameter = wfpg::protocol::PostgresParameter;
using PostgresError = wfpg::protocol::PostgresError;
using WFPostgresTask = wfpg::WFPostgresTask;
using postgres_callback_t = wfpg::postgres_callback_t;
using WFPostgresConnection = wfpg::WFPostgresConnection;
using WFPostgresTaskFactory = wfpg::WFPostgresTaskFactory;
using PostgresStatus = wfpg::PostgresStatus;
using PostgresValue = wfpg::PostgresValue;
using wfpg::get_status;

/**
 * Global shortcut factory functions matching official Workflow naming conventions:
 *   create_postgres_task(url, retry_max, callback)
 *   create_postgres_task(uri, retry_max, callback)
 */
inline WFPostgresTask *create_postgres_task(const std::string& url,
                                            int retry_max,
                                            postgres_callback_t callback)
{
    return wfpg::WFPostgresTaskFactory::create_postgres_task(url, retry_max, std::move(callback));
}

inline WFPostgresTask *create_postgres_task(const ParsedURI& uri,
                                            int retry_max,
                                            postgres_callback_t callback)
{
    return wfpg::WFPostgresTaskFactory::create_postgres_task(uri, retry_max, std::move(callback));
}

#endif // _WF_POSTGRES_CLIENT_H_
