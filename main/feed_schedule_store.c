#include "feed_schedule_store.h"

#include <string.h>

#include "esp_log.h"
#include "feeder_amount.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "sched_store";
static const char *NVS_NS = "feeder";
static const char *NVS_KEY = "schedule2";
static const char *NVS_KEY_LEGACY = "schedule";

static void set_defaults(feed_schedule_t *schedule)
{
    memset(schedule, 0, sizeof(*schedule));
    schedule->enabled = false;
    schedule->start_h = 8;
    schedule->start_m = 0;
    schedule->end_h = 20;
    schedule->end_m = 0;
    schedule->times_per_day = 2;
    schedule->amount_tenths = FEEDER_AMOUNT_TENTHS_DEFAULT;
    strncpy(schedule->timezone, "UTC0", sizeof(schedule->timezone) - 1);
}

static esp_err_t load_blob(nvs_handle_t handle, const char *key, feed_schedule_t *out, size_t *len)
{
    *len = sizeof(*out);
    return nvs_get_blob(handle, key, out, len);
}

static esp_err_t try_load_schedule(feed_schedule_t *out)
{
    set_defaults(out);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(*out);
    err = load_blob(handle, NVS_KEY, out, &len);
    if (err == ESP_OK) {
        nvs_close(handle);
        return ESP_OK;
    }

    feed_schedule_t legacy = {0};
    len = sizeof(legacy);
    err = load_blob(handle, NVS_KEY_LEGACY, &legacy, &len);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "legacy load failed: %s", esp_err_to_name(err));
        set_defaults(out);
        return err;
    }

    legacy.amount_tenths = feeder_amount_seconds_to_tenths(legacy.amount_tenths);
    *out = legacy;
    feed_schedule_store_save(out);
    ESP_LOGI(TAG, "migrated legacy schedule to tenths");
    return ESP_OK;
}

esp_err_t feed_schedule_store_init(void)
{
    return ESP_OK;
}

esp_err_t feed_schedule_store_load(feed_schedule_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t err = try_load_schedule(out);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        set_defaults(out);
    }

    out->timezone[sizeof(out->timezone) - 1] = '\0';
    out->aquapilot_host[sizeof(out->aquapilot_host) - 1] = '\0';
    ESP_LOGI(TAG, "schedule loaded (enabled=%d %02u:%02u-%02u:%02u x%u %.1fs)",
             out->enabled, out->start_h, out->start_m, out->end_h, out->end_m, out->times_per_day,
             (double)feeder_amount_tenths_to_seconds(out->amount_tenths));
    return ESP_OK;
}

esp_err_t feed_schedule_store_save(const feed_schedule_t *schedule)
{
    if (schedule == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY, schedule, sizeof(*schedule));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "schedule saved");
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}
