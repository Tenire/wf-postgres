#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include "workflow/URIParser.h"
#include "WFPostgresConnection.h"

int WFPostgresConnection::init(const std::string& url, SSL_CTX *ssl_ctx)
{
    std::string query;
    ParsedURI uri;

    if (URIParser::parse(url, uri) >= 0)
    {
        if (uri.query)
        {
            query = uri.query;
            query += '&';
        }

        query += "transaction=INTERNAL_CONN_ID_" + std::to_string(this->id);
        free(uri.query);
        uri.query = strdup(query.c_str());
        if (uri.query)
        {
            this->uri = std::move(uri);
            this->ssl_ctx = ssl_ctx;
            return 0;
        }
    }
    else if (uri.state == URI_STATE_INVALID)
        errno = EINVAL;

    return -1;
}
