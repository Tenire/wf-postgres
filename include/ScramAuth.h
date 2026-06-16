#ifndef _WF_POSTGRES_SCRAM_AUTH_H_
#define _WF_POSTGRES_SCRAM_AUTH_H_

#include <string>

namespace protocol {

class ScramAuth {
public:
    ScramAuth() {}
    void init(const std::string& user, const std::string& pass);
    std::string generate_client_first_message();
    std::string generate_client_final_message(const std::string& server_first_message);
    bool verify_server_signature(const std::string& server_final_message);

private:
    std::string user;
    std::string password;
    std::string client_nonce;
    std::string client_first_message_bare;
    std::string auth_message;
    std::string salted_password_cache;
};

} // namespace protocol

#endif // _WF_POSTGRES_SCRAM_AUTH_H_
