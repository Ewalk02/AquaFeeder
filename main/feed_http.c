#include "feed_http.h"

#include <stdio.h>
#include <string.h>

#include "aquafeeder_time.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "feeder_amount.h"
#include "feed_scheduler.h"
#include "stepper_motor.h"

static const char *TAG = "feed_http";

#define FEED_MAX_TENTHS FEEDER_AMOUNT_TENTHS_MAX

static esp_err_t send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status == 200 ? "200 OK" : status == 409 ? "409 Conflict" : "503 Service Unavailable");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool read_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    if (buf_len == 0) {
        return false;
    }

    const int total = req->content_len;
    if (total <= 0) {
        buf[0] = '\0';
        return true;
    }
    if ((size_t)total >= buf_len) {
        return false;
    }

    int received = 0;
    while (received < total) {
        const int chunk = httpd_req_recv(req, buf + received, total - received);
        if (chunk <= 0) {
            return false;
        }
        received += chunk;
    }
    buf[received] = '\0';
    return true;
}

static bool validate_schedule(const feed_schedule_t *schedule)
{
    if (schedule->start_h > 23 || schedule->end_h > 23 || schedule->start_m > 59 || schedule->end_m > 59) {
        return false;
    }
    if (schedule->times_per_day < 1 || schedule->times_per_day > 12) {
        return false;
    }
    if (schedule->amount_tenths < FEEDER_AMOUNT_TENTHS_MIN || schedule->amount_tenths > FEED_MAX_TENTHS) {
        return false;
    }
    return true;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    feed_schedule_t schedule = {0};
    time_t next_feed_epoch = 0;
    feed_scheduler_get_status(&schedule, &next_feed_epoch);

    feed_activity_t activity = {0};
    feed_scheduler_get_feed_activity(&activity);

    const bool clock_ready = aquafeeder_time_is_ready();
    const bool feeding = feed_scheduler_is_feeding();

    char json[512];
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"clock_ready\":%s,\"schedule_enabled\":%s,\"amount_seconds\":%.1f,"
             "\"times_per_day\":%u,\"next_feed_epoch\":%lld,\"feeding\":%s,"
             "\"feed_seconds\":%.1f,\"feed_elapsed_ms\":%u,\"feed_duration_ms\":%u,"
             "\"feed_steps\":%u,\"feed_reason\":\"%s\"}",
             feeding ? "feeding" : "idle", clock_ready ? "true" : "false", schedule.enabled ? "true" : "false",
             (double)feeder_amount_tenths_to_seconds(schedule.amount_tenths), (unsigned)schedule.times_per_day,
             (long long)next_feed_epoch, feeding ? "true" : "false",
             (double)feeder_amount_tenths_to_seconds(activity.amount_tenths), (unsigned)activity.elapsed_ms,
             (unsigned)activity.duration_ms, (unsigned)activity.step_count, activity.reason);

    if (!clock_ready) {
        return send_json(req, 503, json);
    }
    return send_json(req, 200, json);
}

static esp_err_t handle_feed(httpd_req_t *req)
{
    char body[64];
    if (!read_body(req, body, sizeof(body))) {
        return send_json(req, 503, "{\"error\":\"bad request\"}");
    }

    uint16_t amount_tenths = 0;
    if (body[0] != '\0') {
        cJSON *root = cJSON_Parse(body);
        if (root == NULL) {
            return send_json(req, 503, "{\"error\":\"invalid json\"}");
        }
        const cJSON *item = cJSON_GetObjectItem(root, "seconds");
        if (!cJSON_IsNumber(item) ||
            !feeder_amount_seconds_value_to_tenths(cJSON_GetNumberValue(item), &amount_tenths)) {
            cJSON_Delete(root);
            return send_json(req, 503, "{\"error\":\"seconds required\"}");
        }
        cJSON_Delete(root);
    }

    if (amount_tenths < FEEDER_AMOUNT_TENTHS_MIN || amount_tenths > FEED_MAX_TENTHS) {
        return send_json(req, 503, "{\"error\":\"seconds out of range\"}");
    }

    const esp_err_t err = feed_scheduler_start_feed(amount_tenths);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_json(req, 409, "{\"error\":\"busy\"}");
    }
    if (err != ESP_OK) {
        return send_json(req, 503, "{\"error\":\"feed failed\"}");
    }

    ESP_LOGI(TAG, "manual feed accepted: %.1f s", (double)feeder_amount_tenths_to_seconds(amount_tenths));
    return send_json(req, 200, "{\"ok\":true}");
}

static esp_err_t handle_schedule(httpd_req_t *req)
{
    char body[512];
    if (!read_body(req, body, sizeof(body))) {
        return send_json(req, 503, "{\"error\":\"bad request\"}");
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return send_json(req, 503, "{\"error\":\"invalid json\"}");
    }

    feed_schedule_t schedule = {0};
    feed_scheduler_get_status(&schedule, NULL);

    const cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    const cJSON *start_h = cJSON_GetObjectItem(root, "start_h");
    const cJSON *start_m = cJSON_GetObjectItem(root, "start_m");
    const cJSON *end_h = cJSON_GetObjectItem(root, "end_h");
    const cJSON *end_m = cJSON_GetObjectItem(root, "end_m");
    const cJSON *times = cJSON_GetObjectItem(root, "times_per_day");
    const cJSON *amount = cJSON_GetObjectItem(root, "amount_seconds");
    const cJSON *timezone = cJSON_GetObjectItem(root, "timezone");
    const cJSON *aquapilot_host = cJSON_GetObjectItem(root, "aquapilot_host");

    if (cJSON_IsBool(enabled)) {
        schedule.enabled = cJSON_IsTrue(enabled);
    }
    if (cJSON_IsNumber(start_h)) {
        schedule.start_h = (uint8_t)start_h->valueint;
    }
    if (cJSON_IsNumber(start_m)) {
        schedule.start_m = (uint8_t)start_m->valueint;
    }
    if (cJSON_IsNumber(end_h)) {
        schedule.end_h = (uint8_t)end_h->valueint;
    }
    if (cJSON_IsNumber(end_m)) {
        schedule.end_m = (uint8_t)end_m->valueint;
    }
    if (cJSON_IsNumber(times)) {
        schedule.times_per_day = (uint8_t)times->valueint;
    }
    if (cJSON_IsNumber(amount)) {
        if (!feeder_amount_seconds_value_to_tenths(cJSON_GetNumberValue(amount), &schedule.amount_tenths)) {
            cJSON_Delete(root);
            return send_json(req, 503, "{\"error\":\"invalid schedule\"}");
        }
    }
    if (cJSON_IsString(timezone) && timezone->valuestring != NULL) {
        strncpy(schedule.timezone, timezone->valuestring, sizeof(schedule.timezone) - 1);
    }
    if (cJSON_IsString(aquapilot_host) && aquapilot_host->valuestring != NULL) {
        strncpy(schedule.aquapilot_host, aquapilot_host->valuestring, sizeof(schedule.aquapilot_host) - 1);
    }

    cJSON_Delete(root);

    if (!validate_schedule(&schedule)) {
        return send_json(req, 503, "{\"error\":\"invalid schedule\"}");
    }

    if (feed_scheduler_apply(&schedule) != ESP_OK) {
        return send_json(req, 503, "{\"error\":\"save failed\"}");
    }

    return send_json(req, 200, "{\"ok\":true}");
}

static esp_err_t handle_test_step(httpd_req_t *req)
{
    char body[64];
    if (!read_body(req, body, sizeof(body))) {
        return send_json(req, 503, "{\"error\":\"bad request\"}");
    }

    uint32_t steps = 40;
    if (body[0] != '\0') {
        cJSON *root = cJSON_Parse(body);
        if (root == NULL) {
            return send_json(req, 503, "{\"error\":\"invalid json\"}");
        }
        const cJSON *item = cJSON_GetObjectItem(root, "steps");
        if (cJSON_IsNumber(item)) {
            steps = (uint32_t)item->valueint;
        }
        cJSON_Delete(root);
    }

    if (steps < 1 || steps > 400) {
        return send_json(req, 503, "{\"error\":\"steps out of range (1-400)\"}");
    }

    if (feed_scheduler_is_feeding()) {
        return send_json(req, 409, "{\"error\":\"busy\"}");
    }

    stepper_run_steps(steps);
    return send_json(req, 200, "{\"ok\":true}");
}

static esp_err_t handle_skip(httpd_req_t *req)
{
    char body[32];
    read_body(req, body, sizeof(body));

    const esp_err_t err = feed_scheduler_skip_next();
    if (err != ESP_OK) {
        return send_json(req, 503, "{\"error\":\"skip failed\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t feed_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handle_status,
    };
    const httpd_uri_t feed_uri = {
        .uri = "/api/feed",
        .method = HTTP_POST,
        .handler = handle_feed,
    };
    const httpd_uri_t schedule_uri = {
        .uri = "/api/schedule",
        .method = HTTP_POST,
        .handler = handle_schedule,
    };
    const httpd_uri_t skip_uri = {
        .uri = "/api/skip",
        .method = HTTP_POST,
        .handler = handle_skip,
    };
    const httpd_uri_t test_step_uri = {
        .uri = "/api/test/step",
        .method = HTTP_POST,
        .handler = handle_test_step,
    };

    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &feed_uri);
    httpd_register_uri_handler(server, &schedule_uri);
    httpd_register_uri_handler(server, &skip_uri);
    httpd_register_uri_handler(server, &test_step_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
