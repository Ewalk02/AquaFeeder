#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "feed_schedule_store.h"

typedef struct {
    bool active;
    uint16_t amount_tenths;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint32_t step_count;
    const char *reason;
} feed_activity_t;

esp_err_t feed_scheduler_init(void);
bool feed_scheduler_is_feeding(void);
bool feed_scheduler_get_status(feed_schedule_t *schedule, time_t *next_feed_epoch);
bool feed_scheduler_get_feed_activity(feed_activity_t *out);

esp_err_t feed_scheduler_apply(const feed_schedule_t *schedule);
esp_err_t feed_scheduler_start_feed(uint16_t amount_tenths);
esp_err_t feed_scheduler_skip_next(void);
