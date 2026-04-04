#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

class FanDriver;
class WiFiManager;

class HttpServer
{
    static constexpr const char *TAG = "http_srv";

public:
    void start(FanDriver *fanDriver, WiFiManager *wifiManager);

private:
    static esp_err_t handleRoot(httpd_req_t *req);
    static esp_err_t handleGetStatus(httpd_req_t *req);
    static esp_err_t handleGetScan(httpd_req_t *req);
    static esp_err_t handlePostFan(httpd_req_t *req);
    static esp_err_t handlePostWifi(httpd_req_t *req);
    static esp_err_t handleCatchAll(httpd_req_t *req);

    FanDriver      *fanDriver   = nullptr;
    WiFiManager    *wifiManager = nullptr;
    httpd_handle_t  server      = nullptr;
};
