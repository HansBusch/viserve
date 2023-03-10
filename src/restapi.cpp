/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "restapi.h"
#include <time.h>
#include <sstream>
#include <list>

#define MAXPATH 1024
static time_t now;
static restIO readCb, writeCb;
static std::list<CacheEntry*> timerList;
std::list<CacheEntry*> gpioList;

CacheEntry ApiRoot[2];
/**
 * Recursively convert a cache entry to Json.
 */
static bool getJson(std::stringstream &buf, CacheEntry *ce, char *jpath, char *jcur, char *jmax)
{
    if (ce->children) {
        buf << '{';
		bool notFirst = false;
        for (CacheEntry *c = ce->children; c->name; c++) {
            if (notFirst) buf << ',';
            buf << '"' << c->name << "\":";
            char *jc = jcur + snprintf(jcur, jmax - jcur, ".%s", c->name);
            notFirst |= getJson(buf, c, jpath, jc, jmax);
        }
        buf << '}';
		return notFirst;
    }
    else if (ce->op != Writeonly) {
        if (ce->target == Vito && ce->timeout < now) {
            readCb(ce->addr, ce->buffer, ce->len);
            if (ce->len == 2) ce->value = ce->val16;        // propagate sign
            ce->timeout = now + ce->refresh;
        }
        switch (ce->type) {
        case Bool:
            buf << (ce->value ? "true" : "false");
            break;
        case Hex:
            for (int i = 0; i < ce->len; i++) {
                char txt[4];
                snprintf(txt, sizeof(txt), "%02x", ce->buffer[i]);
                buf << txt;
            }
            break;
        default:
            buf << (ce->value / (double)ce->scale);
            break;
        }
		return true;
    }
	return false;
}

/**
 * Recursive lookup of a cache entry from the path
 */
CacheEntry *lookup(const char *path, CacheEntry *ce)
{
    const char *end = strchr(path, '/');
    if (end == 0) end = path + strlen(path);
    for (CacheEntry *c = ce->children; c->name; c++) {
        if (strlen(c->name) == end - path && !memcmp(c->name, path, end - path)) {
            if (*end == 0 || end[1] == 0) return c;
            if (c->children) return lookup(end + 1, c);
            return 0;
        }
    }
    return 0;
}

/**
 * Handler for serving rest GET and PUT calls.
 * GET uses GetJson for returning simple and complex items.
 * PUT only supports setting of individual items.
 */
MHD_Result onRestApi(struct MHD_Connection *connection, const char *url, bool write, const char *data, size_t *dataSize)
{
    CacheEntry *ce = strlen(url) <= 5 ? ApiRoot : lookup(url + 5, ApiRoot);
    if (!ce) {
        const char* fault = "<html><body>Resource not found</body></html>";
        auto response = MHD_create_response_from_buffer(strlen(fault), (void*)fault, MHD_RESPMEM_PERSISTENT);
        auto ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }
	std::stringstream sbuf;
    now = time(0);
    if (!write) {
        if (ce->op == Writeonly) {
            const char* fault = "<html><body>Resource is write only.</body></html>";
            auto response = MHD_create_response_from_buffer(strlen(fault), (void*)fault, MHD_RESPMEM_PERSISTENT);
            auto ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
            MHD_destroy_response(response);
            return ret;
        }
		char jpathbuf[MAXPATH];     ///< Must be larger than longest possible path.
		getJson(sbuf, ce, jpathbuf, jpathbuf, jpathbuf + sizeof(jpathbuf));
	}
	else {
        if (*dataSize > 0) {
            const char* fault = 0;
            if (ce->children) fault = "<html><body>Writing complex types not supported.</body></html>";
            if (ce->op == Readonly) fault = "<html><body>Resource is readonly.</body></html>";
            if (dataSize == 0) fault = "<html><body>Incompatible payload.</body></html>";
            if (fault) {
                auto response = MHD_create_response_from_buffer(strlen(fault), (void*)fault, MHD_RESPMEM_PERSISTENT);
                auto ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
                MHD_destroy_response(response);
                return ret;
            }
            double val = atof(data);
            uint32_t ival = 0;
            int size = 2;
            switch (ce->type) {
            case Int:
                ival = (int)val;
                break;
            case Centi:
                ival = (int)(val * 100 + 0.5);
                break;
            case Deci:
                ival = (int)(val * 10 + 0.5);
                break;
            case Milli:
                ival = (int)(val * 1000 + 0.5);
                break;
            case Half:
                ival = (int)(val * 2 + 0.5);
                break;
            case Bool:
                ival = data[0] == 't' ? 1 : 0;
                break;

            }
            if (writeCb(ce->addr, (uint8_t*)&ival, ce->len) == 0) {
                memcpy(ce->buffer, &ival, ce->len);              // simulate by writing to cache
            }

            if (ce->op == Writeonly) ce->timeout = now + ce->refresh;
            *dataSize = 0;      // libmicrohttpd needs this to allow sending response in next call!
            return MHD_YES;
        }
	}
    struct MHD_Response * response = MHD_create_response_from_buffer(sbuf.str().length(),
        (void*)sbuf.str().c_str(), MHD_RESPMEM_MUST_COPY);
    auto ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    return ret;
}

/**
 * Load an xml entry including all it's children.
 * Uses recursion for loading children.
 */
static void loadApi(CacheEntry* ce, const pugi::xml_node& node, int defaultRefresh)
{
    ce->name = node.name();
    size_t n = std::distance(node.children().begin(), node.children().end());
    if (n > 0) {
        auto cce = (CacheEntry*)calloc(n + 1, sizeof(CacheEntry));       // zero termination
        ce->children = cce;
        auto c = node.first_child();
        for (size_t i = 0; i < n; i++, c = c.next_sibling()) loadApi(&cce[i], c, defaultRefresh);
    }
    else {
        ce->addr = strtoul(node.attribute("addr").as_string(), 0, 16);
        auto type = node.attribute("type").as_string();
        int len = 2;
        ce->scale = node.attribute("scale").as_int(1);
        switch (type[0]) {
        case 'b':
            ce->type = Bool;
            len = 1;
            break;
        case 'c':
            ce->type = Centi;
            ce->scale = 100;
            break;
        case 'd':
            ce->type = Deci;
            ce->scale = 10;
            break;
        case 'h':
            ce->type = (type[1] == 'a') ? Half : Hex;
            if (type[1] == 'a') ce->scale = 2;
            len = 1;
            break;
        case 'm':
            ce->type = Milli;
            ce->scale = 1000;
            break;
        default:
            ce->type = Int;
            break;
        }
        ce->len = node.attribute("len").as_int(len);
        auto op = node.attribute("operation").as_string();
        ce->refresh = node.attribute("refresh").as_int(defaultRefresh);
        ce->op = Readonly;
        if (op) {
            switch (op[0]) {
            case 'r':
                if (op[1] == 'w') ce->op = ReadWrite;
                break;
            case 'w':
                ce->op = Writeonly;
                break;
            case 'p': {     // pulse
                ce->op = Writeonly;
                auto duration = node.attribute("duration");
                if (duration) {
                    ce->refresh = duration.as_int();
                    timerList.push_back(ce);
                }
                break;
            }
            }
        }
        int gpio = node.attribute("gpio").as_int(-1);
        if (gpio >= 0) {
            ce->addr = gpio;
            ce->target = node.attribute("frequency") ? GPIO_Frequency : GPIO_Counter;
            gpioList.push_back(ce);
        }
    }
}

void loadRestApi(CacheEntry* ce, const pugi::xml_node& node, int defaultRefresh, restIO read, restIO write)
{
    readCb = read;
    writeCb = write;
    loadApi(ce, node, defaultRefresh);
}

/**
 * Check for any pending pulse to be switched off
 */
void onRestTimer()
{
    auto now = time(0);
    uint32_t off = 0;
    for (auto it = timerList.begin(); it != timerList.end(); it++) {
        if ((*it)->timeout && (*it)->timeout < now) {
            (*it)->timeout = 0;
            writeCb((*it)->addr, &off, (*it)->len);
        }
    }
}
