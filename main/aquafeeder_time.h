#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AQUAFEEDER_TIMEZONE_MAX 64
#define MIN_VALID_EPOCH         1700000000L

void aquafeeder_time_init(void);
void aquafeeder_time_apply_timezone(const char *timezone);
bool aquafeeder_time_is_ready(void);
