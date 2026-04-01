#include "FanDriver.hpp"

#include <esp_log.h>
#include <esp_matter.h>

// ── Range mapping for kOffLowMedHigh (Matter spec §4.4.6.3.1 / §4.4.6.6.1) ───
//
//  PercentSetting / SpeedSetting (SpeedMax=100, so both are identical values):
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

    // Configure power MOSFET gate pin – LOW = off, HIGH = on
    gpio_config_t power_gpio_cfg = {
        .pin_bit_mask = (1ULL << FAN_POWER_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&power_gpio_cfg));
    gpio_set_level(FAN_POWER_GPIO, 0);  // off until a non-zero speed is set

    ESP_LOGI(TAG, "Fan driver initialised – PWM on GPIO %d at %lu Hz", FAN_PWM_GPIO, PWM_FREQ_HZ);

    // Configure PCNT units to count tachometer pulses (one per fan)
    const gpio_num_t tachGpios[3] = { FAN1_TACH_GPIO, FAN2_TACH_GPIO, FAN3_TACH_GPIO };
    for (int i = 0; i < 3; i++)
    {
        const pcnt_unit_config_t unit_cfg = {
            .low_limit  = -1,
            .high_limit = 32767,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &this->tachUnits[i]));

        const pcnt_chan_config_t chan_cfg = {
            .edge_gpio_num  = tachGpios[i],
            .level_gpio_num = -1,
        };
        pcnt_channel_handle_t chan = nullptr;
        ESP_ERROR_CHECK(pcnt_new_channel(this->tachUnits[i], &chan_cfg, &chan));
        // Count on rising edge only
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_HOLD));

        // Reject glitch pulses shorter than 1 µs (noise from PWM switching)
        const pcnt_glitch_filter_config_t filter_cfg = {
            .max_glitch_ns = 1000,
        };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(this->tachUnits[i], &filter_cfg));

        ESP_ERROR_CHECK(pcnt_unit_enable(this->tachUnits[i]));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(this->tachUnits[i]));
        ESP_ERROR_CHECK(pcnt_unit_start(this->tachUnits[i]));
    }

    // 1-second periodic timer to sample pulse counts and log RPM
    const esp_timer_create_args_t timer_args = {
        .callback = &FanDriver::tachTimerCb,
        .arg      = this,
        .name     = "tach_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &this->tachTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(this->tachTimer, 1000000 /* µs */));
}

// ── Tachometer sampling ───────────────────────────────────────────────────────

void FanDriver::tachTimerCb(void *arg)
{
    static_cast<FanDriver *>(arg)->readAndLogRpm();
}

void FanDriver::readAndLogRpm()
{
    int counts[3];
    for (int i = 0; i < 3; i++)
    {
        pcnt_unit_get_count(this->tachUnits[i], &counts[i]);
        pcnt_unit_clear_count(this->tachUnits[i]);
    }
    // 4-pin fans emit 2 pulses per revolution; sampled over 1 second.
    // RPM = (pulses / 2) * 60 = pulses * 30
    ESP_LOGI(TAG, "Fan RPM – fan1: %d  fan2: %d  fan3: %d",
             counts[0] * 30, counts[1] * 30, counts[2] * 30);
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

    if (this->updatingAttibutesInCallback)
        return ESP_OK;

    if (attribute_id == FanControl::Attributes::FanMode::Id)
    {
        // Writing On is deprecated — treat as High (spec §4.4.6.1).
        // Patch val so the framework stores kHigh, not kOn.
        if (val->val.u8 == (uint8_t)FanControl::FanModeEnum::kOn)
            val->val.u8 = (uint8_t)FanControl::FanModeEnum::kHigh;
        uint8_t newMode = val->val.u8;

        ESP_LOGI(TAG, "FanMode -> %u", newMode);
        this->fanPercentSetting = fanModeDefaultPercent(newMode);

        this->updatingAttibutesInCallback = true;
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, this->fanPercentSetting);
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttibutesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::PercentSetting::Id)
    {
        const uint8_t newPct = val->val.u8;
        if (newPct == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.3)

        ESP_LOGI(TAG, "PercentSetting -> %u", newPct);
        this->fanPercentSetting = newPct;

        this->updatingAttibutesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(this->fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(this->fanPercentSetting));
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttibutesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::SpeedSetting::Id)
    {
        const uint8_t newSpeed = val->val.u8;
        if (newSpeed == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.6)

        ESP_LOGI(TAG, "SpeedSetting -> %u", newSpeed);
        this->fanPercentSetting = newSpeed;

        this->updatingAttibutesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(this->fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(this->fanPercentSetting));
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttibutesInCallback = false;
    }
    else
    {
        return ESP_OK;  // Not a handled attribute
    }

    this->applyFanState();
    return ESP_OK;
}

// ── Apply hardware state ──────────────────────────────────────────────────────

void FanDriver::applyFanState()
{
    using namespace chip::app::Clusters;
    this->setDutyCycle(this->fanPercentSetting);
}

// ── PWM helper ────────────────────────────────────────────────────────────────

void FanDriver::setDutyCycle(uint8_t percent)
{
    if (percent > 100)
        percent = 100;

    gpio_set_level(FAN_POWER_GPIO, percent > 0 ? 1 : 0);

    const uint32_t duty = (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
    ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
    ESP_LOGI(TAG, "Fan PWM: %u%% (duty %lu/%lu)", percent, duty, PWM_MAX_DUTY);
}
