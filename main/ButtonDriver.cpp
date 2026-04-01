#include "ButtonDriver.hpp"
#include "Constants.hpp"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>

void ButtonDriver::init(uint16_t fanEndpointId, DisplayDriver& displayDriver)
{
    this->fanEndpointId = fanEndpointId;
    this->displayDriver = &displayDriver;

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

    ESP_LOGI(TAG, "Buttons initialised");
    return;
}

void ButtonDriver::buttonClickCallback(void *handle, void *userData)
{
    using namespace chip::app::Clusters;

    ButtonDriver* thisInstance = (ButtonDriver*)userData;

    if (thisInstance->displayDriver->getActiveScreen() == DisplayDriver::Screen::Info)
    {
        ESP_LOGI(TAG, "Button click: return to main screen");
        thisInstance->displayDriver->setActiveScreen(DisplayDriver::Screen::Main);
        return;
    }

    ESP_LOGI(TAG, "Button click: toggle fan");
    uint16_t endpointId = thisInstance->fanEndpointId;
    uint32_t clusterId = FanControl::Id;
    uint32_t attributeId = FanControl::Attributes::FanMode::Id;

    esp_matter::attribute_t *attribute = esp_matter::attribute::get(endpointId, clusterId, attributeId);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    esp_matter::attribute::get_val(attribute, &val);
    // Toggle between presets
    val.val.u8 = (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kOff) ? (uint8_t)FanControl::FanModeEnum::kLow
               : (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kLow) ? (uint8_t)FanControl::FanModeEnum::kMedium
               : (val.val.u8 == (uint8_t)FanControl::FanModeEnum::kMedium) ? (uint8_t)FanControl::FanModeEnum::kHigh
               : (uint8_t)FanControl::FanModeEnum::kOff;
    esp_matter::attribute::update(endpointId, clusterId, attributeId, &val);
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
        esp_matter::factory_reset();
        thisInstance->performFactoryReset = false;
    }
}
