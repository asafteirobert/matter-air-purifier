#include "ButtonDriver.hpp"
#include "FanDriver.hpp"
#include "Constants.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>

void ButtonDriver::init(uint16_t fanEndpointId, DisplayDriver& displayDriver, FanDriver& fanDriver)
{
    this->fanEndpointId = fanEndpointId;
    this->displayDriver = &displayDriver;
    this->fanDriver     = &fanDriver;

    this->infoScreenEventArgs = { .long_press = { .press_time = 2000 } };
    this->factoryResetEventArgs = { .long_press = { .press_time = 8000 } };

    const button_config_t buttonConfig =
    {
        .long_press_time  = 2000,
        .short_press_time = 50,
    };
    const button_gpio_config_t boardButtonGpioConfig =
    {
        .gpio_num           = BOARD_BUTTON_GPIO,
        .active_level       = 0,
        .enable_power_save  = false,
        .disable_pull       = false,
    };

    const button_gpio_config_t panelButtonGpioConfig =
    {
        .gpio_num           = PANEL_BUTTON_GPIO,
        .active_level       = 0,
        .enable_power_save  = false,
        .disable_pull       = false,
    };

    if (iot_button_new_gpio_device(&buttonConfig, &boardButtonGpioConfig, &this->boardButtonHandle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create button device");
        return;
    }

    esp_err_t err = ESP_OK;
    err |= iot_button_register_cb(this->boardButtonHandle, BUTTON_SINGLE_CLICK, NULL, buttonClickCallback, this);
    err |= iot_button_register_cb(this->boardButtonHandle, BUTTON_LONG_PRESS_START, &this->infoScreenEventArgs, buttonLongPressInfoCallback, this);
    err |= iot_button_register_cb(this->boardButtonHandle, BUTTON_LONG_PRESS_START, &this->factoryResetEventArgs, buttonLongPressFactoryResetCallback, this);
    err |= iot_button_register_cb(this->boardButtonHandle, BUTTON_PRESS_UP, NULL, buttonPressUpCallback, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set button callbacks");
        return;
    }

    if (iot_button_new_gpio_device(&buttonConfig, &panelButtonGpioConfig, &this->panelButtonHandle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create button device");
        return;
    }

    err = ESP_OK;
    err |= iot_button_register_cb(this->panelButtonHandle, BUTTON_SINGLE_CLICK, NULL, buttonClickCallback, this);
    err |= iot_button_register_cb(this->panelButtonHandle, BUTTON_LONG_PRESS_START, &this->infoScreenEventArgs, buttonLongPressInfoCallback, this);
    err |= iot_button_register_cb(this->panelButtonHandle, BUTTON_LONG_PRESS_START, &this->factoryResetEventArgs, buttonLongPressFactoryResetCallback, this);
    err |= iot_button_register_cb(this->panelButtonHandle, BUTTON_PRESS_UP, NULL, buttonPressUpCallback, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set button callbacks");
        return;
    }

    // Filter reset button
    const button_gpio_config_t filterResetGpioConfig =
    {
        .gpio_num           = FILTER_RESET_BUTTON_GPIO,
        .active_level       = 0,
        .enable_power_save  = false,
        .disable_pull       = false,
    };
    if (iot_button_new_gpio_device(&buttonConfig, &filterResetGpioConfig, &this->filterResetButtonHandle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create filter reset button device");
        return;
    }
    if (iot_button_register_cb(this->filterResetButtonHandle, BUTTON_SINGLE_CLICK, NULL, filterResetButtonCallback, this) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set filter reset button callback");
        return;
    }

    ESP_LOGI(TAG, "Buttons initialised");
    return;
}

void ButtonDriver::buttonClickCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;

    if (thisInstance->displayDriver->getActiveScreen() == DisplayDriver::Screen::Info ||
        thisInstance->displayDriver->getActiveScreen() == DisplayDriver::Screen::Comission)
    {
        ESP_LOGI(TAG, "Button click: return to main screen");
        thisInstance->displayDriver->setActiveScreen(DisplayDriver::Screen::Main);
        return;
    }

    ESP_LOGI(TAG, "Button click: toggle fan");

    uint8_t cur = thisInstance->fanDriver->getFanPercentSetting();
    uint8_t next = (cur == 0)   ? 33
                 : (cur <= 33)  ? 66
                 : (cur <= 66)  ? 100
                 : 0;
    thisInstance->fanDriver->setFanPercentSetting(next);
}

void ButtonDriver::buttonLongPressInfoCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;
    ESP_LOGI(TAG, "Button held 2s: show info screen");
    thisInstance->displayDriver->setActiveScreen(DisplayDriver::Screen::Info);
}

void ButtonDriver::buttonLongPressFactoryResetCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;
    ESP_LOGI(TAG, "Button held 8s: release to factory reset");
    thisInstance->performFactoryReset = true;
    thisInstance->displayDriver->setActiveScreen(DisplayDriver::Screen::FactoryReset);
}

void ButtonDriver::buttonPressUpCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;
    if (thisInstance->performFactoryReset)
    {
        ESP_LOGI(TAG, "Starting factory reset");
        nvs_handle_t nvs;
        if (nvs_open("standalone", NVS_READWRITE, &nvs) == ESP_OK)
        {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        esp_restart();
        thisInstance->performFactoryReset = false;
    }
}

void ButtonDriver::filterResetButtonCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;
    ESP_LOGI(TAG, "Filter reset button pressed – resetting filter usage counter");
    thisInstance->fanDriver->resetFilterCounter();
}
