/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "restapi.h"
#include <time.h>
#include <sstream>
#include <list>

static time_t now;
#define MAX_BUF 2048
/**
  * Recursively convert a cache entry to Json.
  */
static bool getMetrics(std::stringstream& buf, CacheEntry* ce, char* jpath, char* jcur, char* jmax, restIO readCb)
{
    jcur += snprintf(jcur, jmax - jcur, "_%s", ce->name);

    if (ce->children) {
        bool notFirst = false;
        for (CacheEntry* c = ce->children; c->name; c++) {
            getMetrics(buf, c, jpath, jcur, jmax, readCb);
        }
        return true;
    }
    else if (ce->op != Writeonly) {
        if (ce->target == Vito && ce->timeout < now) {
            readCb(ce->addr, ce->buffer, ce->len);
            ce->timeout = now + ce->refresh;
        }
        buf << "# TYPE " << jpath << " gauge\n" << jpath << ' ';
        switch (ce->type) {
        case Bool:
            buf << (ce->value ? '1' : '0');
            break;
        default:
            buf << (ce->value / (double)ce->scale);
            break;
        }
        buf << '\n';
        return true;
    }
    return false;
}


/**
 * Handler for serving OpenMetrics GET scrape calls.
 */
MHD_Result onMetrics(struct MHD_Connection *connection, const char *url, const char* root, restIO readCb)
{
    CacheEntry *ce = lookup(root, ApiRoot);
    if (!ce) {
        const char* fault = "<html><body>Resource not found</body></html>";
        auto response = MHD_create_response_from_buffer(strlen(fault), (void*)fault, MHD_RESPMEM_PERSISTENT);
        auto ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }
	std::stringstream sbuf;
    now = time(0);
	char buffer[MAX_BUF] = "vito";     ///< Must be larger than longest possible path.
    getMetrics(sbuf, ce, buffer, buffer + strlen(buffer), buffer + sizeof(buffer), readCb);

    struct MHD_Response * response = MHD_create_response_from_buffer(sbuf.str().length(),
        (void*)sbuf.str().c_str(), MHD_RESPMEM_MUST_COPY);
    auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    return ret;
}

