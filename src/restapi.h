/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <microhttpd.h>
#include <cstring>
#include "pugixml/pugixml.hpp"
#include <stdint.h>

enum Type { Int, Half, Deci, Centi, Milli, Bool, Hex };
enum Operation { Readonly, ReadWrite, Writeonly };

/**
 * Serves as binary representation of the REST api and as cache.
 */
struct CacheEntry {
    const char *name;       // Name of the entity
    time_t refresh;         // Caching duration. Time in seconds until the value needs to be read from the device. Stores pulse duration for write only.
    CacheEntry *children;   // Child nodes.
    uint32_t addr;          // 16 bit address
    enum Type type;         // Fixed point, boolean and hex data conversion rool to be applied on read/write
    enum Operation op;      // Default is readonly. 
    union { 
        int value;          // Value in little endian matching the API transmission order
        uint8_t buffer[16]; // Buffer size should be sufficient to cache data
    };
    time_t timeout;         // time until the current value is valid
    int len;                // command length
};
extern CacheEntry ApiRoot[2];

typedef int (*restIO)(int addr, void* buffer, size_t size);

MHD_Result onRestApi(struct MHD_Connection *connection, const char *url, bool write, const char *data, size_t *dataSize);
void loadRestApi(CacheEntry* ce, const pugi::xml_node& node, int defaultRefresh, restIO read, restIO write);
void onRestTimer();
CacheEntry* lookup(const char* path, CacheEntry* ce);
