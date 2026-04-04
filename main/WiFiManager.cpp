#include "WiFiManager.hpp"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <mdns.h>
#include <nvs.h>
#include <lwip/sockets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <string.h>

static WiFiManager *s_instance = nullptr;
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT      = BIT1;
static int s_retry_count = 0;
static const int WIFI_MAX_RETRY = 5;

// ── WiFi event handler ────────────────────────────────────────────────────────

void WiFiManager::wifiEventHandler(void *arg, esp_event_base_t base,
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

bool WiFiManager::loadCredentials(char *ssid, size_t ssidLen,
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

void WiFiManager::saveCredentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return;

    nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs, NVS_KEY_PASS, pass);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "WiFi credentials saved: ssid=%s", ssid);
}

// ── WiFi STA connection ───────────────────────────────────────────────────────

bool WiFiManager::connectSTA(const char *ssid, const char *password)
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

void WiFiManager::startMDNS()
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

void WiFiManager::dnsTask(void * /*arg*/)
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

void WiFiManager::startCaptivePortal()
{
    xTaskCreate(dnsTask, "dns_captive", 4096, nullptr, 5, &this->dnsTaskHandle);
}

// ── WiFi AP mode ──────────────────────────────────────────────────────────────

void WiFiManager::startAP()
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
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    // APSTA allows the STA interface to run scans while the AP is active
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID=%s  IP=%s", ssid, this->ipAddr);
    this->startCaptivePortal();
}

// ── Public init ───────────────────────────────────────────────────────────────

void WiFiManager::init()
{
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
}
