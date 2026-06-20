#include "PostgresAuth.h"
#include "ScramAuth.h"
#include "PostgresWireUtil.h"
#include <openssl/evp.h>
#include "WFPostgresError.h"
#include "PostgresInternal.h"

namespace wfpg {
namespace protocol {

PostgresAuth::PostgresAuth() : scram_auth_(nullptr) {}

PostgresAuth::~PostgresAuth() {
    delete scram_auth_;
}

static std::string pg_md5_encrypt(const std::string& user, const std::string& pass, const std::string& salt)
{
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    char hex1[33];
    char hex2[33];
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const EVP_MD* md = EVP_md5();
    
    std::string s1 = pass + user;
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, s1.c_str(), s1.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    for (unsigned int i = 0; i < hash_len && i < 16; i++) {
        sprintf(hex1 + i * 2, "%02x", hash[i]);
    }
    
    std::string s2 = std::string(hex1, 32) + salt;
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, s2.c_str(), s2.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    for (unsigned int i = 0; i < hash_len && i < 16; i++) {
        sprintf(hex2 + i * 2, "%02x", hash[i]);
    }
    
    EVP_MD_CTX_free(ctx);
    
    return std::string("md5") + std::string(hex2, 32);
}

int PostgresAuth::process_auth_request(const uint8_t *payload, size_t payload_size, PostgresResponse *resp) {
    if (payload_size < 4) {
        resp->set_error_fatal("Authentication frame too short");
        return -1;
    }

    uint32_t auth_type = PostgresWireUtil::read_uint32(payload);
    const uint8_t *p = payload + 4;
    const uint8_t *end = payload + payload_size;

    if (auth_type == 0) { // AuthenticationOk
        return 0;
    } else if (auth_type == 3) { // CleartextPassword
        std::string auth_payload = resp->get_pass();
        auth_payload.push_back('\0');
        std::string feedback_msg;
        feedback_msg.append("p", 1);
        PostgresWireUtil::write_uint32(feedback_msg, 4 + auth_payload.size());
        feedback_msg += auth_payload;
        resp->feedback(feedback_msg.c_str(), feedback_msg.size());
    } else if (auth_type == 5) { // AuthenticationMD5Password
        if (end - p < 4) {
            resp->set_error_fatal("AuthenticationMD5Password frame too short");
            return -1;
        }
        std::string salt((const char*)p, 4);
        std::string auth_payload = pg_md5_encrypt(resp->get_user(), resp->get_pass(), salt);
        auth_payload.push_back('\0');
        std::string feedback_msg;
        feedback_msg.append("p", 1);
        PostgresWireUtil::write_uint32(feedback_msg, 4 + auth_payload.size());
        feedback_msg += auth_payload;
        resp->feedback(feedback_msg.c_str(), feedback_msg.size());
    } else if (auth_type == 10) { // AuthenticationSASL
        bool supports_scram_sha_256 = false;
        while (p < end && *p != '\0') {
            std::string mech;
            if (!PostgresWireUtil::read_cstring(p, end, &mech)) break;
            if (mech == "SCRAM-SHA-256") supports_scram_sha_256 = true;
        }

        if (!supports_scram_sha_256) {
            resp->set_error_fatal("Server does not support SCRAM-SHA-256");
            PostgresInternalAccess::set_internal_error(resp, WFT_ERR_POSTGRES_UNSUPPORTED_AUTH);
            return -1;
        }

        if (!scram_auth_) scram_auth_ = new ScramAuth();
        scram_auth_->init(resp->get_user(), resp->get_pass());

        std::string mechanism = "SCRAM-SHA-256";
        std::string client_first_message_bare = scram_auth_->generate_client_first_message();
        
        if (client_first_message_bare.empty()) {
            resp->set_error_fatal("Failed to generate SCRAM client first message (RNG failure)");
            return -1;
        }

        std::string client_first_message_full = "n,," + client_first_message_bare;

        std::string auth_payload = mechanism;
        auth_payload.push_back('\0');
        PostgresWireUtil::write_uint32(auth_payload, client_first_message_full.size());
        auth_payload += client_first_message_full;

        std::string feedback_msg;
        feedback_msg.append("p", 1);
        PostgresWireUtil::write_uint32(feedback_msg, 4 + auth_payload.size());
        feedback_msg += auth_payload;

        resp->feedback(feedback_msg.c_str(), feedback_msg.size());
    } else if (auth_type == 11) { // AuthenticationSASLContinue
        std::string server_first_message((const char*)p, end - p);
        if (!scram_auth_) {
            resp->set_error_fatal("Unexpected SASLContinue without SCRAM state");
            return -1;
        }
        std::string client_final_message = scram_auth_->generate_client_final_message(server_first_message);

        if (client_final_message.empty()) {
            resp->set_error_fatal("SCRAM client final message generation failed (invalid server nonce or PBKDF2 error)");
            return -1;
        }

        std::string auth_payload = client_final_message;
        std::string feedback_msg;
        feedback_msg.append("p", 1);
        PostgresWireUtil::write_uint32(feedback_msg, 4 + auth_payload.size());
        feedback_msg += auth_payload;

        resp->feedback(feedback_msg.c_str(), feedback_msg.size());
    } else if (auth_type == 12) { // AuthenticationSASLFinal
        std::string server_final_message((const char*)p, end - p);
        if (!scram_auth_) {
            resp->set_error_fatal("Unexpected SASLFinal without SCRAM state");
            return -1;
        }
        if (!scram_auth_->verify_server_signature(server_final_message)) {
            resp->set_error_fatal("SCRAM server signature verification failed");
            return -1;
        }
    } else {
        resp->set_error_fatal("Unsupported authentication request type: " + std::to_string(auth_type));
        PostgresInternalAccess::set_internal_error(resp, WFT_ERR_POSTGRES_UNSUPPORTED_AUTH);
        return -1;
    }
    return 0;
}

} // namespace protocol
} // namespace wfpg
