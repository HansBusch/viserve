/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <time.h>
#include <stdint.h>
#include <list>

struct DebounceFilter
{
    static const int MAX = 4;
    uint64_t timestamps[MAX];
    uint64_t last;  // timestamp of last sent
    uint16_t min;   // minimum duration in milliseconds
    bool enabled;
    uint8_t fill;   // number of items read (<255)
    uint16_t ratio; // ratio between longest and shortest gap to be accepted
};

int gpio_init();
/**
 * Poll for gpio event. Method blocks up to 100ms in case no events arrive.
 */
void gpio_poll(time_t& now);

/**
 * Get the gpio filter. 
 * @param index Range zero to 63 supported
 */
DebounceFilter* gpio_filter(unsigned int index);
