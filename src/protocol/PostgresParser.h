#ifndef _WF_POSTGRES_PARSER_H_
#define _WF_POSTGRES_PARSER_H_

#include <string>
#include <map>
#include <vector>
#include "PostgresTypes.h"
#include "PostgresResult.h"

#include "PostgresStream.h"

namespace wfpg {
namespace protocol {

enum PostgresBackendEventType {
    PG_EVENT_AUTH,
    PG_EVENT_ERROR,
    PG_EVENT_PARAMETER_STATUS,
    PG_EVENT_BACKEND_KEY,
    PG_EVENT_NEGOTIATE_PROTOCOL,
    PG_EVENT_READY,
    PG_EVENT_NOTIFICATION,
    PG_EVENT_ROW_DESCRIPTION,
    PG_EVENT_DATA_ROW,
    PG_EVENT_COMMAND_COMPLETE,
    PG_EVENT_COPY_IN,
    PG_EVENT_COPY_OUT,
    PG_EVENT_COPY_DATA,
    PG_EVENT_COPY_DONE,
    PG_EVENT_COPY_FAIL,
    PG_EVENT_NOTICE,
    PG_EVENT_EMPTY_QUERY,
    PG_EVENT_PARSE_COMPLETE,
    PG_EVENT_BIND_COMPLETE,
    PG_EVENT_CLOSE_COMPLETE,
    PG_EVENT_PARAMETER_DESCRIPTION,
    PG_EVENT_NO_DATA,
    PG_EVENT_UNKNOWN
};

struct PostgresBackendEvent {
    PostgresBackendEventType type;
    const uint8_t *payload;
    size_t payload_size;
};

class PostgresParser {
public:
    static PostgresBackendEvent parse_backend_event(const PostgresFrame& frame);
    static int parse_error_response(const uint8_t *data, size_t len, PostgresError *error);
    static int parse_parameter_status(const uint8_t *data, size_t len, std::map<std::string, std::string>& parameters);
    static int parse_backend_key_data(const uint8_t *data, size_t len, int32_t *pid, int32_t *secret_key, std::string *secret_data);
    static int parse_notification(const uint8_t *data, size_t len, std::vector<PostgresNotification> *notifications);
    static int parse_negotiation_response(const uint8_t *data, size_t len, uint32_t *version, std::vector<std::string> *unsupported_options);

    static int parse_row_description(const uint8_t *data, size_t len, std::vector<PostgresField> *fields);
    static int parse_command_complete(const uint8_t *data, size_t len, std::string *command_tag);
    static int parse_data_row(const uint8_t *data, size_t len, std::vector<PostgresCell> *row, const std::vector<PostgresField>& fields);
    static int parse_ready_for_query(const uint8_t *data, size_t len, char *transaction_state);
};

} // namespace protocol
} // namespace wfpg

#endif
