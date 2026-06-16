#include "ScramAuth.h"
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

namespace protocol {

static std::string base64_encode(const unsigned char *buffer, size_t length) {
    size_t out_len = 4 * ((length + 2) / 3);
    char *out = new char[out_len + 1];
    int ret = EVP_EncodeBlock((unsigned char *)out, buffer, length);
    out[ret] = '\0';
    std::string res(out, ret);
    delete[] out;
    return res;
}

static std::string base64_decode(const std::string& b64) {
    size_t in_len = b64.size();
    if (in_len == 0) return "";
    
    size_t out_max = 3 * in_len / 4;
    unsigned char* out = new unsigned char[out_max];
    
    int ret = EVP_DecodeBlock(out, (const unsigned char*)b64.c_str(), in_len);
    if (ret < 0) {
        delete[] out;
        return "";
    }
    
    size_t pad_len = 0;
    if (in_len > 0 && b64[in_len - 1] == '=') pad_len++;
    if (in_len > 1 && b64[in_len - 2] == '=') pad_len++;
    
    std::string res((char*)out, ret - pad_len);
    delete[] out;
    return res;
}

void ScramAuth::init(const std::string& user, const std::string& pass) {
    this->user = user;
    this->password = pass;
}

std::string ScramAuth::generate_client_first_message() {
    unsigned char rand_bytes[16];
    if (RAND_bytes(rand_bytes, sizeof(rand_bytes)) != 1) {
        return ""; // RNG failure
    }
    client_nonce = base64_encode(rand_bytes, sizeof(rand_bytes));
    client_first_message_bare = "n=" + user + ",r=" + client_nonce;
    return client_first_message_bare;
}

std::string ScramAuth::generate_client_final_message(const std::string& server_first_message) {
    // Parse Server First Message: r=nonce,s=salt,i=iterations
    std::string server_nonce, salt_b64;
    int iterations = -1;
    
    size_t pos = 0, next_pos = 0;
    while (next_pos != std::string::npos) {
        next_pos = server_first_message.find(',', pos);
        std::string part = server_first_message.substr(pos, next_pos - pos);
        if (part.size() > 2) {
            if (part[0] == 'r' && part[1] == '=') server_nonce = part.substr(2);
            else if (part[0] == 's' && part[1] == '=') salt_b64 = part.substr(2);
            else if (part[0] == 'i' && part[1] == '=') {
                try {
                    iterations = std::stoi(part.substr(2));
                } catch (...) {
                    return ""; // iterations parse failed
                }
            }
        }
        pos = next_pos + 1;
    }

    if (server_nonce.empty() || salt_b64.empty() || iterations <= 0) {
        return ""; // Missing or invalid r/s/i parameters
    }

    if (server_nonce.substr(0, client_nonce.size()) != client_nonce) {
        return ""; // Invalid nonce
    }

    std::string salt = base64_decode(salt_b64);
    if (salt.empty() && !salt_b64.empty()) {
        return ""; // base64 decode failed
    }
    
    unsigned char salted_password[32];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                      (const unsigned char*)salt.data(), salt.size(),
                      iterations, EVP_sha256(),
                      32, salted_password) != 1) {
        return ""; // PBKDF2 failed
    }

    salted_password_cache.assign((char*)salted_password, 32);

    unsigned char client_key[32];
    unsigned int len;
    HMAC(EVP_sha256(), salted_password, 32,
         (const unsigned char*)"Client Key", 10,
         client_key, &len);

    unsigned char stored_key[32];
    SHA256(client_key, 32, stored_key);

    std::string client_final_without_proof = "c=biws,r=" + server_nonce;
    auth_message = client_first_message_bare + "," + 
                   server_first_message + "," + 
                   client_final_without_proof;

    unsigned char client_signature[32];
    HMAC(EVP_sha256(), stored_key, 32,
         (const unsigned char*)auth_message.data(), auth_message.size(),
         client_signature, &len);

    unsigned char client_proof[32];
    for (int i = 0; i < 32; i++) {
        client_proof[i] = client_key[i] ^ client_signature[i];
    }

    return client_final_without_proof + ",p=" + base64_encode(client_proof, 32);
}

bool ScramAuth::verify_server_signature(const std::string& server_final_message) {
    if (salted_password_cache.size() != 32 || auth_message.empty()) {
        return false;
    }

    if (server_final_message.size() < 2 || server_final_message[0] != 'v' || server_final_message[1] != '=') {
        return false;
    }
    std::string server_signature_b64 = server_final_message.substr(2);
    std::string server_signature = base64_decode(server_signature_b64);

    unsigned char server_key[32];
    unsigned int len;
    HMAC(EVP_sha256(), (const unsigned char*)salted_password_cache.data(), 32,
         (const unsigned char*)"Server Key", 10,
         server_key, &len);

    unsigned char expected_server_signature[32];
    HMAC(EVP_sha256(), server_key, 32,
         (const unsigned char*)auth_message.data(), auth_message.size(),
         expected_server_signature, &len);

    if (server_signature.size() != 32) return false;
    return CRYPTO_memcmp(server_signature.data(), expected_server_signature, 32) == 0;
}

} // namespace protocol
