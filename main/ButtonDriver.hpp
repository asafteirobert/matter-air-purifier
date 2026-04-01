#pragma once
#include <button_gpio.h>
#include <iot_button.h>
#include "DisplayDriver.hpp"

class ButtonDriver
{
    static constexpr char *TAG = "app_button_driver";
    public:
        void init(uint16_t fanEndpointId, DisplayDriver& displayDriver);
    private:
        static void buttonClickCallback(void* handle, void* userData);
        static void buttonLongPressHoldCallback(void *handle, void *userData);
        static void buttonPressUpCallback(void *handle, void *userData);
        static void panelButtonDoubleClickCallback(void *handle, void *userData);

        button_handle_t boardButtonHandle = nullptr;
        button_handle_t panelButtonHandle = nullptr;
        uint16_t fanEndpointId = 0;
        DisplayDriver* displayDriver = nullptr;
        bool performFactoryReset = false;
};