#include "feed_scheduler.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aquafeeder_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feeder_amount.h"
#include "feeder_notify.h"
#include "feeder_slots.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "stepper_motor.h"

static const char *TAG = "scheduler";

#define CHECK_INTERVAL_MS       5000
#define FEED_TRIGGER_WINDOW_S   45
#define MIN_SCHEDULED_GAP_S     60

static feed_schedule_t s_schedule;
static SemaphoreHandle_t s_lock;
static volatile bool s_feeding;
static bool s_skip_next;
static int s_skip_slot = -1;
static int s_last_fed_yday = -1;
static int s_last_fed_slot = -1;
static int64_t s_last_feed_epoch = 0;
static uint16_t s_feed_tenths_requested;
static const char *s_feed_reason = "";

static int minutes_of_day(uint8_t hour, uint8_t minute)
{
    return (int)hour * 60 + (int)minute;
}

static bool get_local_time_parts(int *yday, int *now_min, int *now_sec)
{
    if (!aquafeeder_time_is_ready()) {
        return false;
    }

    const time_t now = time(NULL);
    struct tm local = {0};
    if (localtime_r(&now, &local) == NULL) {
        return false;
    }

    *yday = local.tm_yday;
    *now_min = local.tm_hour * 60 + local.tm_min;
    *now_sec = local.tm_sec;
    return true;
}

static int seconds_until_slot(int now_min, int now_sec, int slot_min)
{
    int delta_min = slot_min - now_min;
    if (delta_min < 0) {
        delta_min += 24 * 60;
    }
    return delta_min * 60 - now_sec;
}

static int next_slot_index(int now_min, int now_sec, int start_min, int end_min, int times)
{
    int best_slot = -1;
    int best_seconds = 24 * 60 * 60;

    for (int slot = 0; slot < times; slot++) {
        const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
        const int secs = seconds_until_slot(now_min, now_sec, slot_min);
        if (secs < best_seconds) {
            best_seconds = secs;
            best_slot = slot;
        }
    }

    return best_slot;
}

static int effective_next_slot(int now_min, int now_sec, int start_min, int end_min, int times)
{
    int slot = next_slot_index(now_min, now_sec, start_min, end_min, times);
    if (slot < 0) {
        return -1;
    }

    if (s_skip_next && slot == s_skip_slot) {
        int best_slot = -1;
        int best_seconds = 24 * 60 * 60;
        for (int candidate = 0; candidate < times; candidate++) {
            if (candidate == s_skip_slot) {
                continue;
            }
            const int slot_min = feeder_slot_minute(candidate, start_min, end_min, times);
            const int secs = seconds_until_slot(now_min, now_sec, slot_min);
            if (secs < best_seconds) {
                best_seconds = secs;
                best_slot = candidate;
            }
        }
        return best_slot;
    }

    return slot;
}

static time_t epoch_for_slot_today(int slot, int start_min, int end_min, int times)
{
    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return 0;
    }

    const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
    const int delta = seconds_until_slot(now_min, now_sec, slot_min);
    return time(NULL) + delta;
}

static bool slot_due_now(int slot, int now_min, int now_sec, int start_min, int end_min, int times)
{
    const int slot_min = feeder_slot_minute(slot, start_min, end_min, times);
    const int slot_sec = slot_min * 60;
    const int now_total = now_min * 60 + now_sec;
    int delta = now_total - slot_sec;
    if (delta < -60) {
        delta += 24 * 60 * 60;
    }
    return delta >= 0 && delta <= FEED_TRIGGER_WINDOW_S;
}

static void run_feed(uint16_t amount_tenths, const char *reason, int slot, int yday)
{
    if (s_feeding || stepper_is_running()) {
        return;
    }

    const time_t now = time(NULL);
    if (s_last_feed_epoch > 0 && (now - s_last_feed_epoch) < MIN_SCHEDULED_GAP_S && slot >= 0) {
        ESP_LOGW(TAG, "scheduled feed suppressed (min gap %d s)", MIN_SCHEDULED_GAP_S);
        return;
    }

    s_feeding = true;
    s_feed_tenths_requested = amount_tenths;
    s_feed_reason = reason;
    if (slot >= 0) {
        s_last_fed_yday = yday;
        s_last_fed_slot = slot;
    }
    s_last_feed_epoch = now;

    ESP_LOGI(TAG, "%s for %.1f s", reason, (double)feeder_amount_tenths_to_seconds(amount_tenths));
    stepper_run_for_ms(feeder_amount_tenths_to_ms(amount_tenths));

    int64_t last_log_ms = 0;
    while (stepper_is_running()) {
        stepper_feed_progress_t progress = {0};
        if (stepper_get_feed_progress(&progress)) {
            const int64_t now_ms = esp_timer_get_time() / 1000LL;
            if (now_ms - last_log_ms >= 1000) {
                last_log_ms = now_ms;
                ESP_LOGI(TAG, "%s: %u/%u ms, %u steps", reason, (unsigned)progress.elapsed_ms,
                         (unsigned)progress.duration_ms, (unsigned)progress.step_count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    s_feeding = false;
    s_feed_reason = "";

    stepper_feed_progress_t progress = {0};
    stepper_get_feed_progress(&progress);
    feeder_notify_feed_complete(amount_tenths, progress.step_count, reason);

    ESP_LOGI(TAG, "%s complete", reason);
}

static void feed_task(void *arg)
{
    const uint16_t amount = (uint16_t)(uintptr_t)arg;
    run_feed(amount, "manual feed", -1, -1);
    vTaskDelete(NULL);
}

static void maybe_run_scheduled_feed(void)
{
    if (!s_schedule.enabled || s_feeding || stepper_is_running()) {
        return;
    }
    if (!aquafeeder_time_is_ready()) {
        return;
    }

    const int start_min = minutes_of_day(s_schedule.start_h, s_schedule.start_m);
    const int end_min = minutes_of_day(s_schedule.end_h, s_schedule.end_m);
    const int times = (int)s_schedule.times_per_day;
    if (times <= 0) {
        return;
    }

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return;
    }

    if (!feeder_in_feeding_window(now_min, start_min, end_min, times)) {
        return;
    }

    for (int slot = 0; slot < times; slot++) {
        if (!slot_due_now(slot, now_min, now_sec, start_min, end_min, times)) {
            continue;
        }
        if (s_last_fed_yday == yday && s_last_fed_slot == slot) {
            continue;
        }
        if (s_skip_next && slot == s_skip_slot) {
            s_skip_next = false;
            s_skip_slot = -1;
            s_last_fed_yday = yday;
            s_last_fed_slot = slot;
            ESP_LOGI(TAG, "skipped scheduled slot %d", slot);
            return;
        }

        run_feed(s_schedule.amount_tenths, "scheduled feed", slot, yday);
        return;
    }
}

static void scheduler_task(void *arg)
{
    (void)arg;

    while (true) {
        maybe_run_scheduled_feed();
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

esp_err_t feed_scheduler_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    feed_schedule_store_load(&s_schedule);
    aquafeeder_time_apply_timezone(s_schedule.timezone);

    if (xTaskCreate(scheduler_task, "feeder_sched", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create scheduler task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "local scheduler started");
    return ESP_OK;
}

bool feed_scheduler_is_feeding(void)
{
    return s_feeding || stepper_is_running();
}

bool feed_scheduler_get_feed_activity(feed_activity_t *out)
{
    if (out == NULL) {
        return false;
    }

    stepper_feed_progress_t progress = {0};
    stepper_get_feed_progress(&progress);

    out->active = feed_scheduler_is_feeding();
    out->amount_tenths = s_feed_tenths_requested;
    out->elapsed_ms = progress.elapsed_ms;
    out->duration_ms = progress.duration_ms;
    out->step_count = progress.step_count;
    out->reason = s_feed_reason != NULL ? s_feed_reason : "";
    return true;
}

bool feed_scheduler_get_status(feed_schedule_t *schedule, time_t *next_feed_epoch)
{
    if (schedule == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    *schedule = s_schedule;
    xSemaphoreGive(s_lock);

    if (next_feed_epoch != NULL) {
        *next_feed_epoch = 0;
        if (schedule->enabled && aquafeeder_time_is_ready()) {
            const int start_min = minutes_of_day(schedule->start_h, schedule->start_m);
            const int end_min = minutes_of_day(schedule->end_h, schedule->end_m);
            const int times = (int)schedule->times_per_day;

            int yday = 0;
            int now_min = 0;
            int now_sec = 0;
            if (get_local_time_parts(&yday, &now_min, &now_sec) && times > 0) {
                const int slot = effective_next_slot(now_min, now_sec, start_min, end_min, times);
                if (slot >= 0) {
                    *next_feed_epoch = epoch_for_slot_today(slot, start_min, end_min, times);
                }
            }
        }
    }

    return true;
}

esp_err_t feed_scheduler_apply(const feed_schedule_t *schedule)
{
    if (schedule == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_schedule = *schedule;
    s_schedule.timezone[sizeof(s_schedule.timezone) - 1] = '\0';
    aquafeeder_time_apply_timezone(s_schedule.timezone);

    esp_err_t err = feed_schedule_store_save(&s_schedule);
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "schedule applied");
    }
    return err;
}

esp_err_t feed_scheduler_start_feed(uint16_t amount_tenths)
{
    if (amount_tenths < FEEDER_AMOUNT_TENTHS_MIN || amount_tenths > FEEDER_AMOUNT_TENTHS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (feed_scheduler_is_feeding()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(feed_task, "manual_feed", 3072, (void *)(uintptr_t)amount_tenths, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t feed_scheduler_skip_next(void)
{
    if (!s_schedule.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!aquafeeder_time_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    const int start_min = minutes_of_day(s_schedule.start_h, s_schedule.start_m);
    const int end_min = minutes_of_day(s_schedule.end_h, s_schedule.end_m);
    const int times = (int)s_schedule.times_per_day;

    int yday = 0;
    int now_min = 0;
    int now_sec = 0;
    if (!get_local_time_parts(&yday, &now_min, &now_sec)) {
        return ESP_ERR_INVALID_STATE;
    }

    const int slot = next_slot_index(now_min, now_sec, start_min, end_min, times);
    if (slot < 0) {
        return ESP_FAIL;
    }

    s_skip_next = true;
    s_skip_slot = slot;
    ESP_LOGI(TAG, "next feeding slot %d will be skipped", slot);
    return ESP_OK;
}
