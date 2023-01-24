/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "gpio.h"
#include <gpiod.h>
#include "vito_io.h"
#include "restapi.h"

static struct gpiod_line_bulk gpios;
static const int MAXLINE = 64;
static DebounceFilter debounceFilter[MAXLINE];

int gpio_init()
{
    struct gpiod_chip* chip;
    struct gpiod_line* line;

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (chip == 0) return logText(0, "io", "Error opening chip");

    gpiod_line_bulk_init(&gpios);

    uint64_t mask = 0;
    for (auto io = gpioList.begin(); io != gpioList.end(); io++) {
        if ((mask & (1 << (*io)->addr)) == 0) {
            line = gpiod_chip_get_line(chip, (*io)->addr);
            if (line == 0) logText(0, "io", "Error opening line %d\n", 17);
            else gpiod_line_bulk_add(&gpios, line);
            mask |= 1 << (*io)->addr;
        }
    }
    int status = gpiod_line_request_bulk_falling_edge_events(&gpios, "viserve");
    if (status) return logText(0, "io", "bulk_register failed %d\n", status);
    return 0;
}

DebounceFilter* gpio_filter(unsigned int index)
{
    if (index >= sizeof(debounceFilter) / sizeof(debounceFilter[0])) return 0;

    return &debounceFilter[index];
}
/**
 * Returns delta to previous emitted event in milliseconds. First emitted is simplified.
 */
static int debounce(unsigned int line, timespec* timestamp)
{
    if (line >= MAXLINE) return 1;
    auto deb = &debounceFilter[line];
    uint64_t ts = timestamp->tv_sec * 1000 + timestamp->tv_nsec / 1000000;
    if (deb->min > 0 && ts - deb->timestamps[0] < deb->min) return 0;          // completely suppress short bounces

    memmove(&deb->timestamps[1], &deb->timestamps[0], (deb->MAX - 1) * sizeof(deb->timestamps[0]));
    deb->timestamps[0] = ts;

    if (deb->fill < 255) deb->fill++;
    if (deb->fill < deb->MAX) return 0;              // wait for first four

    uint64_t max = 0;
    for (int i = 0; i < deb->MAX - 1; i++) {
        auto d = deb->timestamps[i] - deb->timestamps[i + 1];
        if (d > max) max = d;
    }
    auto d0 = (int)(ts - deb->timestamps[1]);

    logText(4, "io", "%2d filter fill=%d max=%d d0=%d", line, deb->fill, (int)max, d0);

    if (deb->fill == deb->MAX) {        // first time report longest
        deb->last = ts;
        return (int)max;
    }

    if (d0 * deb->ratio > max) {        // do only forward events better than ratio
        d0 = ts - deb->last;
        deb->last = ts;
        return d0;
    }
    return 0;
}

static time_t last;
void gpio_poll(time_t &now)
{
    if (now != last) {
        //
        // Adjust frequency in case of slowing or stopping counter
        //
        for (auto io = gpioList.begin(); io != gpioList.end(); io++) {
            if ((*io)->target == GPIO_Frequency && (*io)->addr < MAXLINE && (*io)->lastTs && (*io)->lastTs < now) {
                int val = (*io)->scale / (now - (*io)->lastTs);
                if (val < (*io)->value) {
                    logText(4, "io", "%2d timeout %d : %d", (*io)->addr, (int)now, (int)(*io)->lastTs);
                    logText(4, "io", "%2d timeout %s %d => %d", (*io)->addr, (*io)->name, (*io)->value, val);
                    (*io)->value = val;
                }
            }
        }
        last = now;
    }
    struct timespec ts = { 0, 999000000 };
    gpiod_line_bulk bulkev;
    int rv = gpiod_line_event_wait_bulk(&gpios, &ts, &bulkev);
    if (rv > 0) {
        logText(5, "io", "%d events received", bulkev.num_lines);
        for (int i = 0; i < bulkev.num_lines; i++) {
            struct gpiod_line_event event;
            rv = gpiod_line_event_read(bulkev.lines[i], &event);
            if (rv == 0) {
                int no = gpiod_line_offset(bulkev.lines[i]);
                int ms = debounce(no, &event.ts);
                logText(3, "io", "%2d d=%d", no, ms);
                if (ms > 0) {
                    for (auto io = gpioList.begin(); io != gpioList.end(); io++) {
                        if (no == (*io)->addr) {
                            if ((*io)->target == GPIO_Counter) (*io)->value++;              // increment
                            else {
                                (*io)->value = (*io)->scale * 1000 / ms;         // estimate frequency
                            }
                            logText(4, "io", "%2d update %s => %d", no, (*io)->name, (*io)->value);
                            (*io)->lastTs = now;
                        }
                    }
                }
            }
        }
    }

}
