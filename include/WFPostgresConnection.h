#ifndef _WFPOSTGRESCONNECTION_H_
#define _WFPOSTGRESCONNECTION_H_

#include <string>
#include <openssl/ssl.h>
#include "workflow/URIParser.h"
#include "PostgresTask.h"

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

    WFPostgresTask *create_disconnect_task(postgres_callback_t callback);

protected:
    void set_ssl_ctx(WFPostgresTask *task) const;

protected:
    ParsedURI uri;
    SSL_CTX *ssl_ctx;
    bool owns_ssl_ctx;
    int id;

public:
    WFPostgresConnection(int id) { 
        this->id = id; 
        this->ssl_ctx = NULL;
        this->owns_ssl_ctx = false;
    }
    
    virtual ~WFPostgresConnection() { 
        this->deinit();
    }
};

} // namespace wfpg

#endif
