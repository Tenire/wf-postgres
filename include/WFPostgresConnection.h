#ifndef _WFPOSTGRESCONNECTION_H_
#define _WFPOSTGRESCONNECTION_H_

#include <string>
#include <type_traits>
#include <openssl/ssl.h>
#include "workflow/URIParser.h"
#include "PostgresTask.h"
#include "PostgresValue.h"

namespace wfpg {

class WFPostgresConnection
{
public:
    int init(const std::string& url)
    {
        return this->init(url, NULL);
    }

    int init(const std::string& url, SSL_CTX *ssl_ctx);

    void deinit() {
        if (this->owns_ssl_ctx && this->ssl_ctx) {
            SSL_CTX_free(this->ssl_ctx);
            this->ssl_ctx = NULL;
            this->owns_ssl_ctx = false;
        }
    }
    WFPostgresTask *create_query_task(const std::string& query,
                                      postgres_callback_t callback);

    WFPostgresTask *create_query_task(const std::string& query,
                                      std::nullptr_t)
    {
        return this->create_query_task(query, postgres_callback_t(nullptr));
    }
    WFPostgresTask *create_query_task(const std::string& query,
                                      const std::vector<protocol::PostgresParameter>& params,
                                      postgres_callback_t callback);
    template <typename T, typename... Args,
              typename = typename std::enable_if<!std::is_convertible<T, postgres_callback_t>::value &&
                                                 !std::is_same<typename std::decay<T>::type, std::nullptr_t>::value>::type>
    WFPostgresTask *create_query_task(const std::string& query,
                                      const T& first,
                                      const Args&... rest)
    {
        return this->create_query_task(query, bind_params(first, rest...), nullptr);
    }

    template <typename T, typename... Args>
    WFPostgresTask *create_query_task(const std::string& query,
                                      postgres_callback_t callback,
                                      const T& first,
                                      const Args&... rest)
    {
        return this->create_query_task(query, bind_params(first, rest...), std::move(callback));
    }

    WFPostgresTask *create_disconnect_task(postgres_callback_t callback);

    // Cancel running query on this connection without exposing raw pid/secret
    WFPostgresTask *create_cancel_task(postgres_callback_t callback);

    char get_last_transaction_state() const { return this->last_tx_state_; }
    bool in_transaction() const { return this->last_tx_state_ == 'T'; }
    bool is_transaction_failed() const { return this->last_tx_state_ == 'E'; }

    int32_t get_backend_pid() const { return this->backend_pid_; }
    const std::string& get_backend_secret_data() const { return this->backend_secret_data_; }

    void set_last_transaction_state(char state) { this->last_tx_state_ = state; }
    void set_ssl_ctx(WFPostgresTask *task) const;

protected:
    ParsedURI uri;
    SSL_CTX *ssl_ctx;
    bool owns_ssl_ctx;
    int id;
    char last_tx_state_;
    int32_t backend_pid_;
    std::string backend_secret_data_;
    std::string raw_url_;

public:
    WFPostgresConnection(int id) { 
        this->id = id; 
        this->ssl_ctx = NULL;
        this->owns_ssl_ctx = false;
        this->last_tx_state_ = 'I';
        this->backend_pid_ = 0;
    }
    virtual ~WFPostgresConnection() { 
        this->deinit();
    }
};

} // namespace wfpg

#endif
