#ifndef _WF_POSTGRES_AUTH_H_
#define _WF_POSTGRES_AUTH_H_

#include "PostgresResponse.h"

namespace wfpg {
namespace protocol {

class ScramAuth;

class PostgresAuth {
public:
    PostgresAuth();
    ~PostgresAuth();

    int process_auth_request(const uint8_t *payload, size_t payload_size, PostgresResponse *resp);

private:
    ScramAuth *scram_auth_;
};

} // namespace protocol
} // namespace wfpg

#endif
