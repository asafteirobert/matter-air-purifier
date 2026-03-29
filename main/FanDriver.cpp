#include "FanDriver.hpp"

#include <esp_log.h>
#include <esp_matter.h>

void FanDriver::init(uint16_t fanEndpointId)
{
    this->fanEndpointId = fanEndpointId;

    ESP_LOGI(TAG, "Fan driver initialised");
}

esp_err_t FanDriver::attributeUpdate(esp_matter::attribute::callback_type_t type,
                                     uint16_t endpoint_id,
                                     uint32_t cluster_id,
                                     uint32_t attribute_id,
                                     esp_matter_attr_val_t *val)
{
    using namespace chip::app::Clusters;

    if (type != esp_matter::attribute::PRE_UPDATE)
    {
        return ESP_OK;
    }

    if (endpoint_id != this->fanEndpointId)
    {
        return ESP_OK;
    }

    if (val == nullptr)
    {
        ESP_LOGE(TAG, "attributeUpdate: val is null");
        return ESP_ERR_INVALID_ARG;
    }

    // TODO

    return ESP_OK;
}

void FanDriver::applyFanState()
{
    // TODO
}
