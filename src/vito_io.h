/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>

int vito_open(char *device);

int vito_init( void );

int vito_read(int addr, void* buffer, size_t size);
int vito_write(int addr, void* buffer, size_t size);

int logText(const char* prefix, const char* fmt, ...);
void logDump(int level, const char* prefix, int addr, const void* data, size_t size);
