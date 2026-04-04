#pragma once

#include <esp_err.h>
#include <esp_event.h>
#include <freertos/task.h>

class WiFiManager
{
    static constexpr const char *TAG = "wifi_mgr";

public:
    static constexpr const char *NVS_NAMESPACE = "standalone";
    static constexpr const char *NVS_KEY_SSID  = "ssid";
    static constexpr const char *NVS_KEY_PASS  = "password";

    void init();
    void saveCredentials(const char *ssid, const char *pass);
    bool isAPMode() const { return this->apMode; }

    // Accessible to static event/task handlers
    char ipAddr[16]   = {};
    char mdnsName[32] = {};
    bool apMode       = false;

private:
    bool loadCredentials(char *ssid, size_t ssidLen, char *pass, size_t passLen);
    bool connectSTA(const char *ssid, const char *password);
    void startAP();
    void startMDNS();
    void startCaptivePortal();

    static void wifiEventHandler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data);
    static void dnsTask(void *arg);

    TaskHandle_t   dnsTaskHandle  = nullptr;
    esp_event_handler_instance_t evtHandlerAny   = nullptr;
    esp_event_handler_instance_t evtHandlerGotIp = nullptr;
};
