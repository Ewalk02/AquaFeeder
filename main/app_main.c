#include "aquafeeder_time.h"
#include "esp_log.h"
#include "feed_http.h"
#include "feed_schedule_store.h"
#include "feed_scheduler.h"
#include "nvs_flash.h"
#include "stepper_motor.h"
#include "wifi_setup.h"

static const char *TAG = "aquafeeder";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "AquaFeeder starting");

    ESP_ERROR_CHECK(feed_schedule_store_init());
    ESP_ERROR_CHECK(stepper_motor_init());
    ESP_ERROR_CHECK(wifi_setup_init());
    aquafeeder_time_init();
    ESP_ERROR_CHECK(feed_scheduler_init());
    ESP_ERROR_CHECK(feed_http_start());

    ESP_LOGI(TAG, "ready");
}
