#ifndef _WFPOSTGRESCONNECTION_H_
#define _WFPOSTGRESCONNECTION_H_

#include <string>
#include <utility>
#include <openssl/ssl.h>
#include "workflow/URIParser.h"
#include "PostgresTask.h"

class WFPostgresConnection
{
public:
    int init(const std::string& url)
    {
        return this->init(url, NULL);
    }

    int init(const std::string& url, SSL_CTX *ssl_ctx);

    void deinit() { }

public:
    protocol::WFPostgresTask *create_query_task(const std::string& query,
                                                protocol::postgres_callback_t callback)
    {
        protocol::WFPostgresTask *task = WFPostgresTaskFactory::create_postgres_task(this->uri, 0, std::move(callback));
        this->set_ssl_ctx(task);
        task->get_req()->set_query(query);
        return task;
    }

    protocol::WFPostgresTask *create_disconnect_task(protocol::postgres_callback_t callback)
    {
        protocol::WFPostgresTask *task = this->create_query_task("", std::move(callback));
        task->get_req()->set_is_disconnect(true);
        this->set_ssl_ctx(task);
        task->set_keep_alive(0);
        return task;
    }

protected:
    void set_ssl_ctx(protocol::WFPostgresTask *task) const
    {
        using PostgresRequest = protocol::PostgresRequest;
        using PostgresResponse = protocol::PostgresResponse;
        auto *t = (WFComplexClientTask<PostgresRequest, PostgresResponse> *)task;
        t->set_ssl_ctx(this->ssl_ctx);
    }

protected:
    ParsedURI uri;
    SSL_CTX *ssl_ctx;
    int id;

public:
    WFPostgresConnection(int id) { this->id = id; }
    virtual ~WFPostgresConnection() { }
};

#endif
