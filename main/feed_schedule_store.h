#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define FEED_SCHEDULE_TIMEZONE_MAX 64
#define FEED_AQUAPILOT_HOST_MAX    64

typedef struct {
    bool enabled;
    uint8_t start_h;
    uint8_t start_m;
    uint8_t end_h;
    uint8_t end_m;
    uint8_t times_per_day;
    uint16_t amount_tenths;
    char timezone[FEED_SCHEDULE_TIMEZONE_MAX];
    char aquapilot_host[FEED_AQUAPILOT_HOST_MAX];
} feed_schedule_t;

esp_err_t feed_schedule_store_init(void);
esp_err_t feed_schedule_store_load(feed_schedule_t *out);
esp_err_t feed_schedule_store_save(const feed_schedule_t *schedule);
