#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_event.h>
#include <freertos/task.h>

class FanDriver;
class DisplayDriver;

class StandaloneMode
{
    static constexpr const char *TAG = "standalone";

public:
    static constexpr const char *NVS_NAMESPACE = "standalone";
    static constexpr const char *NVS_KEY_SSID  = "ssid";
    static constexpr const char *NVS_KEY_PASS  = "password";

    esp_err_t init(FanDriver &fanDriver, DisplayDriver &displayDriver);
    bool isAPMode() const { return this->apMode; }

    // Accessible to static event handlers
    char ipAddr[16]    = {};
    char mdnsName[32]  = {};
    bool apMode        = false;

private:
    bool loadCredentials(char *ssid, size_t ssidLen, char *pass, size_t passLen);
    bool connectSTA(const char *ssid, const char *password);
    void startAP();
    void startMDNS();
    void startCaptivePortal();
    void startHTTPServer();

    static void wifiEventHandler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data);

    static void      dnsTask(void *arg);
    static esp_err_t handleRoot(httpd_req_t *req);
    static esp_err_t handleGetStatus(httpd_req_t *req);
    static esp_err_t handleGetScan(httpd_req_t *req);
    static esp_err_t handlePostFan(httpd_req_t *req);
    static esp_err_t handlePostWifi(httpd_req_t *req);
    static esp_err_t handleCatchAll(httpd_req_t *req);

    FanDriver     *fanDriver     = nullptr;
    DisplayDriver *displayDriver = nullptr;
    httpd_handle_t server        = nullptr;
    TaskHandle_t   dnsTaskHandle = nullptr;

    esp_event_handler_instance_t evtHandlerAny   = nullptr;
    esp_event_handler_instance_t evtHandlerGotIp = nullptr;
};

