#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define STEPPER_FULL_STEPS_PER_REV 200

typedef struct {
    bool active;
    uint32_t duration_ms;
    uint32_t elapsed_ms;
    uint32_t step_count;
} stepper_feed_progress_t;

esp_err_t stepper_motor_init(void);
void stepper_run_for_ms(uint32_t ms);
void stepper_run_steps(uint32_t steps);
bool stepper_is_running(void);
bool stepper_get_feed_progress(stepper_feed_progress_t *out);
