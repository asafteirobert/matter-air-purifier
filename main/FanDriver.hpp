#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "Constants.hpp"
#include "DisplayDriver.hpp"

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
    static constexpr uint32_t         RPM_READ_INTERVAL_MS = 2000;

public:
    void init(uint16_t fanEndpointId, DisplayDriver& displayDriver);
    void syncFromMatter();
    esp_err_t attributeUpdate(esp_matter::attribute::callback_type_t type,
                              uint16_t endpoint_id,
                              uint32_t cluster_id,
                              uint32_t attribute_id,
                              esp_matter_attr_val_t *val);
    void setFanPercentSetting(uint8_t newSetting);
    void resetFilterCounter();

private:
    void applyFanState();
    void readAndSendRpm();
    void saveFilterCounter();
    static void tachTimerCb(void *arg);

    static constexpr uint16_t NVS_FLUSH_INTERVAL = 300;  // save every 300 tach callbacks (~10 min)

    uint16_t fanEndpointId  = 0;
    DisplayDriver* displayDriver = nullptr;
    uint8_t  fanPercentSetting = 0;    // 0–100
    bool updatingAttributesInCallback = false;

    pcnt_unit_handle_t tachUnits[3] = {};
    esp_timer_handle_t tachTimer    = nullptr;

    uint64_t filterUsageCounter = 0;
    uint16_t nvsFlushCounter    = 0;
};
