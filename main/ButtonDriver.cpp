#include "ButtonDriver.hpp"
#include "FanDriver.hpp"
#include "Constants.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#ifndef CONFIG_STANDALONE_MODE
#include <esp_matter.h>
#endif

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
        .gpio_num = BOARD_BUTTON_GPIO,
        .active_level = 0,
    };

    const button_gpio_config_t panelButtonGpioConfig =
    {
        .gpio_num = PANEL_BUTTON_GPIO,
        .active_level = 0,
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
        .gpio_num    = FILTER_RESET_BUTTON_GPIO,
        .active_level = 0,
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

#ifdef CONFIG_STANDALONE_MODE
    // In standalone mode, toggle fan speed directly without Matter
    uint8_t cur = thisInstance->fanDriver->getFanPercentSetting();
    uint8_t next = (cur == 0)   ? 33
                 : (cur <= 33)  ? 66
                 : (cur <= 66)  ? 100
                 : 0;
    thisInstance->fanDriver->setFanPercentSetting(next);
#else
    using namespace chip::app::Clusters;
    uint16_t endpointId = thisInstance->fanEndpointId;
    uint32_t clusterId = FanControl::Id;
    uint32_t attributeId = FanControl::Attributes::FanMode::Id;

    esp_matter::attribute_t *attribute = esp_matter::attribute::get(endpointId, clusterId, attributeId);
    if (attribute == nullptr)
    {
        ESP_LOGE(TAG, "Failed to get FanMode attribute");
        return;
    }

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    esp_matter::attribute::get_val(attribute, &val);
    // Toggle between presets
    val.val.u8 = (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kOff)    ? (uint8_t)FanControl::FanModeEnum::kLow
               : (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kLow)    ? (uint8_t)FanControl::FanModeEnum::kMedium
               : (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kMedium) ? (uint8_t)FanControl::FanModeEnum::kHigh
               : (uint8_t)FanControl::FanModeEnum::kOff;
    esp_matter::attribute::update(endpointId, clusterId, attributeId, &val);
#endif
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
#ifdef CONFIG_STANDALONE_MODE
        // In standalone mode, erase WiFi credentials and reboot
        nvs_handle_t nvs;
        if (nvs_open("standalone", NVS_READWRITE, &nvs) == ESP_OK)
        {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        esp_restart();
#else
        esp_matter::factory_reset();
#endif
        thisInstance->performFactoryReset = false;
    }
}

void ButtonDriver::filterResetButtonCallback(void *handle, void *userData)
{
    ButtonDriver* thisInstance = (ButtonDriver*)userData;
    ESP_LOGI(TAG, "Filter reset button pressed – resetting filter usage counter");
    thisInstance->fanDriver->resetFilterCounter();
}
