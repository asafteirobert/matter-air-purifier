#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include "driver/gpio.h"
#include "Constants.hpp"

class FanDriver
{
    static constexpr const char *TAG = "fan_driver";

public:
    void init(uint16_t fanEndpointId);
    esp_err_t attributeUpdate(esp_matter::attribute::callback_type_t type,
                              uint16_t endpoint_id,
                              uint32_t cluster_id,
                              uint32_t attribute_id,
                              esp_matter_attr_val_t *val);
    void applyFanState();

private:
    uint16_t fanEndpointId = 0;
};
