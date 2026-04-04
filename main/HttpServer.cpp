#include "HttpServer.hpp"
#include "WiFiManager.hpp"
#include "FanDriver.hpp"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <cJSON.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── Embedded web UI (index.html linked by CMakeLists EMBED_TXTFILES) ─────────

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static HttpServer *s_instance = nullptr;

// ── Handlers ──────────────────────────────────────────────────────────────────

esp_err_t HttpServer::handleRoot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start);
}

esp_err_t HttpServer::handleGetStatus(httpd_req_t *req)
{
    FanDriver   *fd = s_instance->fanDriver;
    WiFiManager *wm = s_instance->wifiManager;

    uint32_t rpm1 = 0, rpm2 = 0, rpm3 = 0;
    fd->getRPM(rpm1, rpm2, rpm3);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "speed",   fd->getFanPercentSetting());
    cJSON_AddNumberToObject(root, "rpm1",    (double)rpm1);
    cJSON_AddNumberToObject(root, "rpm2",    (double)rpm2);
    cJSON_AddNumberToObject(root, "rpm3",    (double)rpm3);
    cJSON_AddNumberToObject(root, "filter",  (double)fd->getFilterPercent());
    cJSON_AddBoolToObject  (root, "ap_mode", wm->apMode ? 1 : 0);
    cJSON_AddStringToObject(root, "ip",      wm->ipAddr);
    cJSON_AddStringToObject(root, "mdns",    wm->mdnsName);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t ret = httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    return ret;
}

esp_err_t HttpServer::handleGetScan(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden          = false;
    scan_cfg.scan_type            = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 50;
    scan_cfg.scan_time.active.max = 120;

    esp_wifi_scan_start(&scan_cfg, true);  // blocking – typically ~1.5 s

    uint16_t count = 20;
    wifi_ap_record_t records[20];
    esp_wifi_scan_get_ap_records(&count, records);

    cJSON *arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < count; i++)
    {
        if (records[i].ssid[0] == '\0') continue;  // skip hidden networks
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", records[i].rssi);
        cJSON_AddBoolToObject  (net, "auth", records[i].authmode != WIFI_AUTH_OPEN ? 1 : 0);
        cJSON_AddItemToArray(arr, net);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t ret = httpd_resp_sendstr(req, json ? json : "[]");
    free(json);
    return ret;
}

esp_err_t HttpServer::handlePostFan(httpd_req_t *req)
{
    char buf[64] = {};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *speed = cJSON_GetObjectItem(root, "speed");
    if (cJSON_IsNumber(speed))
    {
        int v = speed->valueint;
        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        s_instance->fanDriver->setFanPercentSetting((uint8_t)v);
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t HttpServer::handlePostWifi(httpd_req_t *req)
{
    char buf[256] = {};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *jSsid = cJSON_GetObjectItem(root, "ssid");
    cJSON *jPass = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(jSsid) || !jSsid->valuestring || !jSsid->valuestring[0])
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    const char *ssid = jSsid->valuestring;
    const char *pass = (cJSON_IsString(jPass) && jPass->valuestring) ? jPass->valuestring : "";

    s_instance->wifiManager->saveCredentials(ssid, pass);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    // Short delay to ensure the response reaches the client before restarting
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t HttpServer::handleCatchAll(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "");
}

// ── Public start ──────────────────────────────────────────────────────────────

void HttpServer::start(FanDriver *fd, WiFiManager *wm)
{
    this->fanDriver   = fd;
    this->wifiManager = wm;
    s_instance = this;

    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.server_port       = 80;
    config.stack_size        = 8192;
    config.max_uri_handlers  = 10;
    config.max_open_sockets  = 3;   // single-user portal; leave sockets for DNS + netifs
    config.uri_match_fn      = httpd_uri_match_wildcard;

    if (httpd_start(&this->server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = handleRoot,      .user_ctx = nullptr },
        { .uri = "/api/status", .method = HTTP_GET,  .handler = handleGetStatus, .user_ctx = nullptr },
        { .uri = "/api/scan",   .method = HTTP_GET,  .handler = handleGetScan,   .user_ctx = nullptr },
        { .uri = "/api/fan",    .method = HTTP_POST, .handler = handlePostFan,   .user_ctx = nullptr },
        { .uri = "/api/wifi",   .method = HTTP_POST, .handler = handlePostWifi,  .user_ctx = nullptr },
        // Catch-all: redirect every unrecognised request to the portal page.
        // Must be registered last so specific handlers above take priority.
        // Registered for GET and HEAD — the methods used by OS captive-portal probes.
        { .uri = "/*",          .method = HTTP_GET,  .handler = handleCatchAll,  .user_ctx = nullptr },
        { .uri = "/*",          .method = HTTP_HEAD, .handler = handleCatchAll,  .user_ctx = nullptr },
    };

    for (const auto &uri : uris)
        httpd_register_uri_handler(this->server, &uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}
