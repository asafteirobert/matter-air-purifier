#pragma once
#include <button_gpio.h>
#include <iot_button.h>
#include "DisplayDriver.hpp"

class FanDriver;

class ButtonDriver
{
    static constexpr const char *TAG = "app_button_driver";
    public:
        void init(uint16_t fanEndpointId, DisplayDriver& displayDriver, FanDriver& fanDriver);
    private:
        static void buttonClickCallback(void* handle, void* userData);
        static void buttonLongPressInfoCallback(void *handle, void *userData);
        static void buttonLongPressFactoryResetCallback(void *handle, void *userData);
        static void buttonPressUpCallback(void *handle, void *userData);
        static void filterResetButtonCallback(void *handle, void *userData);

        button_handle_t boardButtonHandle       = nullptr;
        button_handle_t panelButtonHandle       = nullptr;
        button_handle_t filterResetButtonHandle = nullptr;
        uint16_t fanEndpointId = 0;
        DisplayDriver* displayDriver = nullptr;
        FanDriver*     fanDriver     = nullptr;
        button_event_args_t infoScreenEventArgs;
        button_event_args_t factoryResetEventArgs;
        bool performFactoryReset = false;
};
