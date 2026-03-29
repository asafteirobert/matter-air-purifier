#include "FanDriver.hpp"

#include <esp_log.h>
#include <esp_matter.h>

// ── Range mapping for kOffLowMedHigh (Matter spec §4.4.6.3.1 / §4.4.6.6.1) ───
//
//  PercentSetting / SpeedSetting (SpeedMax=100, so both are identical values):
//    0        → Off
//    1 – 33   → Low
//    34 – 66  → Medium
//    67 – 100 → High
//
// The exact breakpoints are implementation-defined; the spec's own illustrative
// example uses equal thirds, which we adopt here.

static uint8_t percentToFanMode(uint8_t pct)
{
    using namespace chip::app::Clusters;
    if (pct == 0)   return (uint8_t)FanControl::FanModeEnum::kOff;
    if (pct <= 33)  return (uint8_t)FanControl::FanModeEnum::kLow;
    if (pct <= 66)  return (uint8_t)FanControl::FanModeEnum::kMedium;
    return                 (uint8_t)FanControl::FanModeEnum::kHigh;
}

// Default PercentSetting when switching to a mode without an existing in-range value.
// We use the top of each range so the fan runs at full power for the selected mode.
static uint8_t fanModeDefaultPercent(uint8_t fanMode)
{
    using namespace chip::app::Clusters;
    switch ((FanControl::FanModeEnum)fanMode)
    {
    case FanControl::FanModeEnum::kLow:    return 33;
    case FanControl::FanModeEnum::kMedium: return 66;
    case FanControl::FanModeEnum::kHigh:   return 100;
    default:                               return 0;
    }
}

static bool isPercentInRange(uint8_t pct, uint8_t fanMode)
{
    return percentToFanMode(pct) == fanMode;
}

// ── Helpers to update single attributes ────────

static void updateAttrU8(uint16_t endpoint, uint32_t cluster, uint32_t attr, uint8_t newVal)
{
    esp_matter_attr_val_t v = esp_matter_uint8(newVal);
    esp_matter::attribute::update(endpoint, cluster, attr, &v);
}

static void updateAttrEnum8(uint16_t endpoint, uint32_t cluster, uint32_t attr, uint8_t newVal)
{
    esp_matter_attr_val_t v = esp_matter_enum8(newVal);
    esp_matter::attribute::update(endpoint, cluster, attr, &v);
}
static void updateAttrNullableU8(uint16_t endpoint, uint32_t cluster, uint32_t attr, uint8_t newVal)
{
    esp_matter_attr_val_t v = esp_matter_nullable_uint8(nullable<uint8_t>(newVal));
    esp_matter::attribute::update(endpoint, cluster, attr, &v);
}


// ── Init ──────────────────────────────────────────────────────────────────────

void FanDriver::init(uint16_t fanEndpointId)
{
    this->fanEndpointId = fanEndpointId;

    // Configure LEDC timer for 25 kHz (standard 4-pin PC fan PWM frequency)
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = PWM_SPEED_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num       = PWM_TIMER,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // Configure LEDC channel on the fan PWM pin, starting at 0% duty
    const ledc_channel_config_t channel_cfg = {
        .gpio_num   = FAN_PWM_GPIO,
        .speed_mode = PWM_SPEED_MODE,
        .channel    = PWM_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

    ESP_LOGI(TAG, "Fan driver initialised – PWM on GPIO %d at %lu Hz", FAN_PWM_GPIO, PWM_FREQ_HZ);
}

// ── Attribute update callback ─────────────────────────────────────────────────

esp_err_t FanDriver::attributeUpdate(esp_matter::attribute::callback_type_t type,
                                     uint16_t endpoint_id,
                                     uint32_t cluster_id,
                                     uint32_t attribute_id,
                                     esp_matter_attr_val_t *val)
{
    using namespace chip::app::Clusters;

    if (type != esp_matter::attribute::PRE_UPDATE)
        return ESP_OK;

    if (endpoint_id != this->fanEndpointId)
        return ESP_OK;

    if (val == nullptr)
    {
        ESP_LOGE(TAG, "attributeUpdate: val is null");
        return ESP_ERR_INVALID_ARG;
    }

    if (cluster_id != FanControl::Id)
        return ESP_OK;

    const uint32_t FC = FanControl::Id;

    if (updatingAttibutesInCallback)
        return ESP_OK;

    if (attribute_id == FanControl::Attributes::FanMode::Id)
    {
        // Writing On is deprecated — treat as High (spec §4.4.6.1).
        // Patch val so the framework stores kHigh, not kOn.
        if (val->val.u8 == (uint8_t)FanControl::FanModeEnum::kOn)
            val->val.u8 = (uint8_t)FanControl::FanModeEnum::kHigh;
        uint8_t newMode = val->val.u8;

        ESP_LOGI(TAG, "FanMode -> %u", newMode);
        fanPercentSetting = fanModeDefaultPercent(newMode);

        updatingAttibutesInCallback = true;
        updateAttrNullableU8(fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, fanPercentSetting);
        updateAttrNullableU8(fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, fanPercentSetting);
        updatingAttibutesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::PercentSetting::Id)
    {
        const uint8_t newPct = val->val.u8;
        if (newPct == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.3)

        ESP_LOGI(TAG, "PercentSetting -> %u", newPct);
        fanPercentSetting = newPct;

        updatingAttibutesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(fanPercentSetting));
        updateAttrNullableU8(fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, fanPercentSetting);
        updatingAttibutesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::SpeedSetting::Id)
    {
        const uint8_t newSpeed = val->val.u8;
        if (newSpeed == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.6)

        ESP_LOGI(TAG, "SpeedSetting -> %u", newSpeed);
        fanPercentSetting = newSpeed;

        updatingAttibutesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(fanPercentSetting));
        updateAttrNullableU8(fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, fanPercentSetting);
        updateAttrU8(fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, fanPercentSetting);
        updatingAttibutesInCallback = false;
    }
    else
    {
        return ESP_OK;  // Not a handled attribute
    }

    applyFanState();
    return ESP_OK;
}

// ── Apply hardware state ──────────────────────────────────────────────────────

void FanDriver::applyFanState()
{
    using namespace chip::app::Clusters;
    setDutyCycle(fanPercentSetting);
}

// ── PWM helper ────────────────────────────────────────────────────────────────

void FanDriver::setDutyCycle(uint8_t percent)
{
    if (percent > 100)
        percent = 100;

    const uint32_t duty = (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
    ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
    ESP_LOGI(TAG, "Fan PWM: %u%% (duty %lu/%lu)", percent, duty, PWM_MAX_DUTY);
}
