#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "Constants.hpp"

class FanDriver
{
    static constexpr const char *TAG = "fan_driver";

    // 25 kHz is the standard PWM frequency for 4-pin PC fans
    static constexpr ledc_mode_t      PWM_SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t     PWM_TIMER      = LEDC_TIMER_0;
    static constexpr ledc_channel_t   PWM_CHANNEL    = LEDC_CHANNEL_0;
    static constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_10_BIT;
    static constexpr uint32_t         PWM_FREQ_HZ    = 25000;
    static constexpr uint32_t         PWM_MAX_DUTY   = (1u << 10) - 1;  // 1023

public:
    void init(uint16_t fanEndpointId);
    esp_err_t attributeUpdate(esp_matter::attribute::callback_type_t type,
                              uint16_t endpoint_id,
                              uint32_t cluster_id,
                              uint32_t attribute_id,
                              esp_matter_attr_val_t *val);
    void applyFanState();

private:
    void setDutyCycle(uint8_t percent);

    uint16_t fanEndpointId  = 0;
    uint8_t  fanPercentSetting = 0;    // 0–100
    bool updatingAttibutesInCallback = false;
};
