#include "aquafeeder_time.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "time";

#define SNTP_RETRY_MS 30000

static bool s_sntp_started;
static bool s_sync_task_started;

static void apply_timezone_default(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
}

static void on_time_sync(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }
    ESP_LOGI(TAG, "SNTP sync OK (epoch %lld)", (long long)tv->tv_sec);
}

static void start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started");
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == IP_EVENT_STA_GOT_IP) {
        start_sntp();
    }
}

static void time_sync_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(SNTP_RETRY_MS));
        if (aquafeeder_time_is_ready()) {
            continue;
        }
        if (!s_sntp_started) {
            if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") != NULL) {
                start_sntp();
            }
            continue;
        }
        ESP_LOGW(TAG, "clock not set, restarting SNTP");
        esp_sntp_restart();
    }
}

static void start_sync_task(void)
{
    if (s_sync_task_started) {
        return;
    }
    if (xTaskCreate(time_sync_task, "time_sync", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create SNTP retry task");
        return;
    }
    s_sync_task_started = true;
}

void aquafeeder_time_init(void)
{
    apply_timezone_default();
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
    start_sync_task();
}

void aquafeeder_time_apply_timezone(const char *timezone)
{
    if (timezone == NULL || timezone[0] == '\0') {
        apply_timezone_default();
        return;
    }
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to %s", timezone);
}

bool aquafeeder_time_is_ready(void)
{
    return time(NULL) >= MIN_VALID_EPOCH;
}
