#include "feeder_notify.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "feeder_amount.h"
#include "feed_schedule_store.h"

static const char *TAG = "feeder_notify";

#define HTTP_TIMEOUT_MS 5000

void feeder_notify_feed_complete(uint16_t amount_tenths, uint32_t steps, const char *reason)
{
    feed_schedule_t schedule = {0};
    if (feed_schedule_store_load(&schedule) != ESP_OK) {
        return;
    }

    if (schedule.aquapilot_host[0] == '\0') {
        ESP_LOGD(TAG, "no AquaPilot host configured, skip notify");
        return;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s/api/feeder/complete", schedule.aquapilot_host);

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"feeding_complete\",\"seconds\":%.1f,\"steps\":%u,\"reason\":\"%s\"}",
             (double)feeder_amount_tenths_to_seconds(amount_tenths), (unsigned)steps,
             reason != NULL ? reason : "");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGW(TAG, "notify failed: no memory");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, (int)strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status >= 200 && status < 300) {
        ESP_LOGI(TAG, "feeding complete sent to %s", schedule.aquapilot_host);
    } else {
        ESP_LOGW(TAG, "notify failed host=%s err=%s status=%d", schedule.aquapilot_host, esp_err_to_name(err),
                 status);
    }
}
