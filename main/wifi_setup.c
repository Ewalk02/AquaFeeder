#include "wifi_setup.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "wifi";

#define RECONNECT_BACKOFF_INITIAL_MS 1000
#define RECONNECT_BACKOFF_MAX_MS     60000

static bool s_connected;
static uint32_t s_reconnect_attempt;
static uint32_t s_backoff_ms = RECONNECT_BACKOFF_INITIAL_MS;
static esp_timer_handle_t s_reconnect_timer;

static void request_wifi_connect(void)
{
    esp_wifi_connect();
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;

    if (s_connected) {
        return;
    }

    s_reconnect_attempt++;
    ESP_LOGI(TAG, "reconnect attempt %lu", (unsigned long)s_reconnect_attempt);
    request_wifi_connect();
}

static void schedule_reconnect(void)
{
    if (s_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = reconnect_timer_cb,
            .name = "wifi_reconn",
        };
        if (esp_timer_create(&args, &s_reconnect_timer) != ESP_OK) {
            request_wifi_connect();
            return;
        }
    }

    esp_timer_stop(s_reconnect_timer);
    ESP_LOGW(TAG, "disconnected, retry in %lu ms", (unsigned long)s_backoff_ms);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_backoff_ms * 1000ULL);

    if (s_backoff_ms < RECONNECT_BACKOFF_MAX_MS) {
        s_backoff_ms *= 2;
        if (s_backoff_ms > RECONNECT_BACKOFF_MAX_MS) {
            s_backoff_ms = RECONNECT_BACKOFF_MAX_MS;
        }
    }
}

static void log_disconnect_reason(int reason)
{
    const char *name = "unknown";
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        name = "no_ap_found";
        break;
    case WIFI_REASON_AUTH_FAIL:
        name = "auth_fail";
        break;
    case WIFI_REASON_ASSOC_FAIL:
        name = "assoc_fail";
        break;
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        name = "handshake_timeout";
        break;
    case WIFI_REASON_CONNECTION_FAIL:
        name = "connection_fail";
        break;
    case WIFI_REASON_BEACON_TIMEOUT:
        name = "beacon_timeout";
        break;
    default:
        break;
    }
    ESP_LOGW(TAG, "disconnected: %s (%d)", name, reason);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_backoff_ms = RECONNECT_BACKOFF_INITIAL_MS;
        request_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;

        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)event_data;
        if (disc != NULL) {
            log_disconnect_reason(disc->reason);
        } else {
            ESP_LOGW(TAG, "disconnected");
        }

        schedule_reconnect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_reconnect_attempt = 0;
        s_backoff_ms = RECONNECT_BACKOFF_INITIAL_MS;
        if (s_reconnect_timer != NULL) {
            esp_timer_stop(s_reconnect_timer);
        }
    }
}

esp_err_t wifi_setup_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_FEEDER_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_FEEDER_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to SSID %s", CONFIG_FEEDER_WIFI_SSID);
    return ESP_OK;
}

bool wifi_setup_is_connected(void)
{
    return s_connected;
}
