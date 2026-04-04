#include "StandaloneMode.hpp"
#include "FanDriver.hpp"
#include "DisplayDriver.hpp"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <mdns.h>
#include <nvs.h>
#include <lwip/sockets.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// ── Embedded web UI (index.html linked by CMakeLists EMBED_TXTFILES) ─────────

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

// ── Internal state ────────────────────────────────────────────────────────────

static StandaloneMode *s_instance = nullptr;
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT      = BIT1;
static int s_retry_count = 0;
static const int WIFI_MAX_RETRY = 5;

// ── WiFi event handler ────────────────────────────────────────────────────────

void StandaloneMode::wifiEventHandler(void *arg, esp_event_base_t base,
                                      int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_count < WIFI_MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "WiFi retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_instance->ipAddr, sizeof(s_instance->ipAddr));
        ESP_LOGI(TAG, "WiFi connected, IP: %s", s_instance->ipAddr);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ── NVS helpers ───────────────────────────────────────────────────────────────

bool StandaloneMode::loadCredentials(char *ssid, size_t ssidLen,
                                     char *pass, size_t passLen)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return false;

    bool ok = false;
    size_t len = ssidLen;
    if (nvs_get_str(nvs, NVS_KEY_SSID, ssid, &len) == ESP_OK && len > 1)
    {
        len = passLen;
        nvs_get_str(nvs, NVS_KEY_PASS, pass, &len);  // password is optional
        ok = true;
    }
    nvs_close(nvs);
    return ok;
}

// ── WiFi STA connection ───────────────────────────────────────────────────────

bool StandaloneMode::connectSTA(const char *ssid, const char *password)
{
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t cfg = {};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT)
        return true;

    ESP_LOGW(TAG, "Failed to connect to WiFi: %s", ssid);
    esp_wifi_stop();
    return false;
}

// ── mDNS ─────────────────────────────────────────────────────────────────────

void StandaloneMode::startMDNS()
{
    snprintf(this->mdnsName, sizeof(this->mdnsName), "airpurifier");

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(this->mdnsName));
    mdns_instance_name_set("Air Purifier");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS started: http://%s.local", this->mdnsName);
}

// ── Captive portal DNS server ─────────────────────────────────────────────────
//
// Listens on UDP 53. Responds to every A-record query with 192.168.4.1 so the
// connecting device's OS-level captive-portal probe lands on our HTTP server.

void StandaloneMode::dnsTask(void * /*arg*/)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "DNS: socket() failed");
        vTaskDelete(nullptr);
        return;
    }

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "DNS: bind() failed");
        close(sock);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "DNS captive portal listening on UDP 53");

    // Answer record appended after the question section:
    // name=ptr(0x0C), TYPE A, CLASS IN, TTL 60s, RDLEN 4, 192.168.4.1
    static const uint8_t ANSWER[] = {
        0xC0, 0x0C,              // name: pointer to offset 12
        0x00, 0x01,              // TYPE A
        0x00, 0x01,              // CLASS IN
        0x00, 0x00, 0x00, 0x3C, // TTL 60 s
        0x00, 0x04,              // RDLENGTH 4
        192,  168,  4,   1,     // 192.168.4.1
    };

    uint8_t buf[512];
    for (;;)
    {
        struct sockaddr_in src = {};
        socklen_t srclen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf) - sizeof(ANSWER), 0,
                           (struct sockaddr *)&src, &srclen);
        if (len < 12) continue;

        // Turn into a response: QR=1 AA=1 RD=1 RA=1, ANCOUNT=1
        buf[2] = 0x85;
        buf[3] = 0x80;
        buf[6] = 0; buf[7] = 1;   // ANCOUNT = 1
        buf[8] = 0; buf[9] = 0;   // NSCOUNT = 0
        buf[10] = 0; buf[11] = 0; // ARCOUNT = 0

        // Walk past the question's QNAME to find where to append the answer
        int p = 12;
        while (p < len && buf[p] != 0) p += buf[p] + 1;
        p += 5; // null label (1) + QTYPE (2) + QCLASS (2)

        if (p + (int)sizeof(ANSWER) <= (int)sizeof(buf))
        {
            memcpy(buf + p, ANSWER, sizeof(ANSWER));
            sendto(sock, buf, p + sizeof(ANSWER), 0,
                   (struct sockaddr *)&src, srclen);
        }
    }
}

void StandaloneMode::startCaptivePortal()
{
    xTaskCreate(dnsTask, "dns_captive", 4096, nullptr, 5, &this->dnsTaskHandle);
}

// ── WiFi AP mode ──────────────────────────────────────────────────────────────

void StandaloneMode::startAP()
{
    this->apMode = true;
    snprintf(this->ipAddr, sizeof(this->ipAddr), "192.168.4.1");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    char ssid[32];
    snprintf(ssid, sizeof(ssid), "AirPurifier-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    memcpy(ap_cfg.ap.ssid, ssid, strlen(ssid));
    ap_cfg.ap.ssid_len      = (uint8_t)strlen(ssid);
    ap_cfg.ap.channel       = 1;
    ap_cfg.ap.authmode      = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    // APSTA allows the STA interface to run scans while the AP is active
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID=%s  IP=%s", ssid, this->ipAddr);
    this->startCaptivePortal();
}

// ── HTTP server ───────────────────────────────────────────────────────────────

esp_err_t StandaloneMode::handleGetScan(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden        = false;
    scan_cfg.scan_type          = WIFI_SCAN_TYPE_ACTIVE;
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

esp_err_t StandaloneMode::handleRoot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start);
}

esp_err_t StandaloneMode::handleGetStatus(httpd_req_t *req)
{
    FanDriver *fd = s_instance->fanDriver;
    uint32_t rpm1 = 0, rpm2 = 0, rpm3 = 0;
    fd->getRPM(rpm1, rpm2, rpm3);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "speed",  fd->getFanPercentSetting());
    cJSON_AddNumberToObject(root, "rpm1",   (double)rpm1);
    cJSON_AddNumberToObject(root, "rpm2",   (double)rpm2);
    cJSON_AddNumberToObject(root, "rpm3",   (double)rpm3);
    cJSON_AddNumberToObject(root, "filter", (double)fd->getFilterPercent());
    cJSON_AddBoolToObject  (root, "ap_mode", s_instance->apMode ? 1 : 0);
    cJSON_AddStringToObject(root, "ip",     s_instance->ipAddr);
    cJSON_AddStringToObject(root, "mdns",   s_instance->mdnsName);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t ret = httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    return ret;
}

esp_err_t StandaloneMode::handlePostFan(httpd_req_t *req)
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

esp_err_t StandaloneMode::handlePostWifi(httpd_req_t *req)
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

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(StandaloneMode::NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_set_str(nvs, StandaloneMode::NVS_KEY_SSID, ssid);
        nvs_set_str(nvs, StandaloneMode::NVS_KEY_PASS, pass);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI("standalone", "WiFi credentials saved: ssid=%s", ssid);
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    // Short delay to ensure the response reaches the client before restarting
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t StandaloneMode::handleCatchAll(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "");
}

void StandaloneMode::startHTTPServer()
{
    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.server_port     = 80;
    config.stack_size      = 8192;
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

// ── Public init ───────────────────────────────────────────────────────────────

esp_err_t StandaloneMode::init(FanDriver &fanDriver, DisplayDriver &displayDriver)
{
    this->fanDriver     = &fanDriver;
    this->displayDriver = &displayDriver;
    s_instance = this;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create both netifs up-front; only the active mode's interface will carry traffic
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr, &this->evtHandlerAny));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr, &this->evtHandlerGotIp));

    // Attempt STA connection if credentials are stored
    char ssid[33] = {};
    char pass[65] = {};
    if (this->loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass)) &&
        this->connectSTA(ssid, pass))
    {
        this->apMode = false;
        this->startMDNS();
    }
    else
    {
        this->startAP();
    }

    this->startHTTPServer();
    return ESP_OK;
}
