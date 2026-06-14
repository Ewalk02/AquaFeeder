#include "stepper_motor.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "stepper";

#define GPIO_STEP 4
#define GPIO_DIR  5
#define GPIO_EN   6

#define PROGRESS_LOG_INTERVAL_MS 1000

static volatile bool s_running;
static volatile uint32_t s_step_count;
static volatile uint32_t s_duration_ms;
static int64_t s_start_us;
static int64_t s_last_log_us;

static int effective_steps_per_rev(void)
{
    return STEPPER_FULL_STEPS_PER_REV * CONFIG_STEPPER_MICROSTEPS;
}

static int step_period_us(void)
{
    const float rpm = (float)CONFIG_STEPPER_TEST_RPM;
    const float steps_per_sec = (rpm / 60.0f) * (float)effective_steps_per_rev();
    if (steps_per_sec <= 0.0f) {
        return 100000;
    }
    return (int)(1000000.0f / steps_per_sec);
}

static void stepper_enable(bool on)
{
#if CONFIG_STEPPER_EN_ACTIVE_LOW
    gpio_set_level(GPIO_EN, on ? 0 : 1);
#else
    gpio_set_level(GPIO_EN, on ? 1 : 0);
#endif
}

esp_err_t stepper_motor_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GPIO_STEP) | (1ULL << GPIO_DIR) | (1ULL << GPIO_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(GPIO_DIR, CONFIG_STEPPER_DIR_INVERT ? 1 : 0);
    gpio_set_level(GPIO_STEP, 0);
    stepper_enable(false);

    ESP_LOGI(TAG, "stepper init STEP=%d DIR=%d EN=%d", GPIO_STEP, GPIO_DIR, GPIO_EN);
    ESP_LOGI(TAG, "config: %d RPM shaft, 1/%d microstep (%d pulses/rev), %d us STEP pulse, EN active-%s",
             CONFIG_STEPPER_TEST_RPM, CONFIG_STEPPER_MICROSTEPS, effective_steps_per_rev(),
             CONFIG_STEPPER_STEP_PULSE_US, CONFIG_STEPPER_EN_ACTIVE_LOW ? "LOW" : "HIGH");
    ESP_LOGI(TAG, "period ~%d us/pulse (~%.1f pulses/s)", step_period_us(),
             1000000.0f / (float)step_period_us());

    return ESP_OK;
}

static void step_once(void)
{
    gpio_set_level(GPIO_STEP, 1);
    esp_rom_delay_us(CONFIG_STEPPER_STEP_PULSE_US);

    gpio_set_level(GPIO_STEP, 0);

    const int period = step_period_us();
    const int remainder = period - CONFIG_STEPPER_STEP_PULSE_US;
    if (remainder > 0) {
        esp_rom_delay_us(remainder);
    }

    s_step_count++;
}

static void log_progress(bool force)
{
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_ms = (uint32_t)((now_us - s_start_us) / 1000LL);

    if (!force && (now_us - s_last_log_us) < (int64_t)PROGRESS_LOG_INTERVAL_MS * 1000LL) {
        return;
    }
    s_last_log_us = now_us;

    const float revs = (float)s_step_count / (float)effective_steps_per_rev();
    ESP_LOGI(TAG, "feed progress: %u/%u ms, %u pulses (%.2f rev)", (unsigned)elapsed_ms, (unsigned)s_duration_ms,
             (unsigned)s_step_count, revs);
}

static void run_steps_for_duration(uint32_t duration_ms)
{
    const int64_t end_us = esp_timer_get_time() + (int64_t)duration_ms * 1000LL;
    const uint32_t expected_steps = (uint32_t)(((uint64_t)duration_ms * 1000ULL) / (uint32_t)step_period_us());

    s_running = true;
    s_step_count = 0;
    s_duration_ms = duration_ms;
    s_start_us = esp_timer_get_time();
    s_last_log_us = s_start_us;

    stepper_enable(true);
    ESP_LOGI(TAG, "feed START: %u ms (~%u pulses expected at %d RPM shaft)", (unsigned)duration_ms,
             (unsigned)expected_steps, CONFIG_STEPPER_TEST_RPM);

    while (esp_timer_get_time() < end_us) {
        step_once();
        log_progress(false);
    }

    log_progress(true);
    stepper_enable(false);
    s_running = false;

    const float revs = (float)s_step_count / (float)effective_steps_per_rev();
    ESP_LOGI(TAG, "feed DONE: %u pulses (%.2f rev) in %u ms", (unsigned)s_step_count, revs, (unsigned)duration_ms);
}

void stepper_run_for_ms(uint32_t ms)
{
    if (s_running || ms == 0) {
        if (s_running) {
            ESP_LOGW(TAG, "feed rejected: motor already running");
        }
        return;
    }

    run_steps_for_duration(ms);
}

void stepper_run_steps(uint32_t steps)
{
    if (s_running || steps == 0) {
        return;
    }

    s_running = true;
    s_step_count = 0;
    s_duration_ms = 0;
    s_start_us = esp_timer_get_time();
    s_last_log_us = s_start_us;

    stepper_enable(true);
    ESP_LOGI(TAG, "jog START: %u pulses at %d RPM shaft", (unsigned)steps, CONFIG_STEPPER_TEST_RPM);

    for (uint32_t i = 0; i < steps; i++) {
        step_once();
    }

    stepper_enable(false);
    s_running = false;

    const float revs = (float)s_step_count / (float)effective_steps_per_rev();
    ESP_LOGI(TAG, "jog DONE: %u pulses (%.2f rev)", (unsigned)s_step_count, revs);
}

bool stepper_is_running(void)
{
    return s_running;
}

bool stepper_get_feed_progress(stepper_feed_progress_t *out)
{
    if (out == NULL) {
        return false;
    }

    out->active = s_running;
    out->duration_ms = s_duration_ms;
    out->step_count = s_step_count;
    if (s_running && s_start_us > 0) {
        out->elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_us) / 1000LL);
    } else if (!s_running && s_duration_ms > 0) {
        out->elapsed_ms = s_duration_ms;
    } else {
        out->elapsed_ms = 0;
    }
    return true;
}
