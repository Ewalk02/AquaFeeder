#pragma once

#include <stdint.h>

void feeder_notify_feed_complete(uint16_t amount_tenths, uint32_t steps, const char *reason);
