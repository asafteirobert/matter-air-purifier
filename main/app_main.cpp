#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ButtonDriver.hpp"
#include "FanDriver.hpp"
#include "DisplayDriver.hpp"
#include "StandaloneMode.hpp"

static const char *TAG = "app_main";

ButtonDriver buttonDriver;
FanDriver fanDriver;
DisplayDriver displayDriver;
StandaloneMode standaloneMode;

// ── Shared startup helpers ────────────────────────────────────────────────────

static void initNVS()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void initAntenna()
{
    // Activate RF switch (active LOW)
    gpio_set_direction(ANTENNA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_ENABLE_GPIO, 0);
    // Select internal antenna
    gpio_set_direction(ANTENNA_CONFIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_CONFIG_GPIO, 0);
}

extern "C" void app_main()
{
    initNVS();
    initAntenna();

    displayDriver.init();
    displayDriver.drawSplashScreen();

    buttonDriver.init(0, displayDriver, fanDriver);
    fanDriver.init(0, displayDriver);

    standaloneMode.init(fanDriver, displayDriver);

    ESP_LOGI(TAG, "Standalone mode running – AP=%d  IP=%s",
             standaloneMode.isAPMode(), standaloneMode.isAPMode() ? "192.168.4.1" : "");

    vTaskDelay(pdMS_TO_TICKS(2000));
    displayDriver.startTask();
    vTaskDelete(nullptr);
}
