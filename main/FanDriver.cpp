#include "FanDriver.hpp"

#include <esp_log.h>
#ifndef CONFIG_STANDALONE_MODE
#include <esp_matter.h>
#endif
#include <nvs.h>

#ifndef CONFIG_STANDALONE_MODE
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

// ── Helpers to update single attributes ──────── (Matter-only)

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
#endif // !CONFIG_STANDALONE_MODE

// ── Init ──────────────────────────────────────────────────────────────────────

void FanDriver::init(uint16_t fanEndpointId, DisplayDriver& displayDriver)
{
    this->fanEndpointId = fanEndpointId;
    this->displayDriver = &displayDriver;

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

    // 2-second periodic timer to sample pulse counts and log RPM
    const esp_timer_create_args_t timer_args = {
        .callback = &FanDriver::tachTimerCb,
        .arg      = this,
        .name     = "tach_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &this->tachTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(this->tachTimer, RPM_READ_INTERVAL_MS * 1000 /* µs */));

    // Load persisted filter usage counter
    nvs_handle_t nvs;
    if (nvs_open("app_data", NVS_READONLY, &nvs) == ESP_OK)
    {
        nvs_get_u64(nvs, "filter_cnt", &this->filterUsageCounter);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Filter usage counter loaded: %llu", (unsigned long long)this->filterUsageCounter);
    this->displayDriver->setFilterUsage(this->filterUsageCounter);
}

#ifndef CONFIG_STANDALONE_MODE
// ── Sync state from Matter NVS on startup ────────────────────────────────────

void FanDriver::syncFromMatter()
{
    using namespace chip::app::Clusters;

    esp_matter_attr_val_t val = {};
    esp_err_t err = esp_matter::attribute::get_val(
        this->fanEndpointId,
        FanControl::Id,
        FanControl::Attributes::FanMode::Id,
        &val);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "syncFromMatter: get_val failed (%d)", err);
        return;
    }

    uint8_t pct = fanModeDefaultPercent(val.val.u8);
    ESP_LOGI(TAG, "syncFromMatter: FanMode=%u -> fanPercentSetting=%u", val.val.u8, pct);
    this->setFanPercentSetting(pct);

    const uint32_t FC = FanControl::Id;
    this->updatingAttributesInCallback = true;
    updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, this->fanPercentSetting);
    updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, this->fanPercentSetting);
    updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
    updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
    this->updatingAttributesInCallback = false;
}
#endif // !CONFIG_STANDALONE_MODE

// ── Tachometer sampling ───────────────────────────────────────────────────────

void FanDriver::tachTimerCb(void *arg)
{
    static_cast<FanDriver *>(arg)->readAndSendRpm();
}

void FanDriver::setFanPercentSetting(uint8_t newSetting)
{
    if (newSetting > 100)
        newSetting = 100;

    this->fanPercentSetting = newSetting;
    this->displayDriver->setFanPercentSetting(newSetting);
    this->applyFanState();
}

void FanDriver::getRPM(uint32_t &rpm1, uint32_t &rpm2, uint32_t &rpm3) const
{
    rpm1 = this->lastRPM[0];
    rpm2 = this->lastRPM[1];
    rpm3 = this->lastRPM[2];
}

uint16_t FanDriver::getFilterPercent() const
{
    if (FILTER_MAX_USAGE == 0) return 0;
    return (uint16_t)((this->filterUsageCounter * 100ULL) / FILTER_MAX_USAGE);
}

void FanDriver::readAndSendRpm()
{
    int counts[3];
    for (int i = 0; i < 3; i++)
    {
        pcnt_unit_get_count(this->tachUnits[i], &counts[i]);
        pcnt_unit_clear_count(this->tachUnits[i]);
    }
    // 4-pin fans emit 2 pulses per revolution; sampled over 2 seconds.
    // RPM = (pulses / 2) * (60 / interval_s) = pulses * 30 * 1000 / interval_ms
    this->lastRPM[0] = (uint32_t)(counts[0] * 30 * 1000 / RPM_READ_INTERVAL_MS);
    this->lastRPM[1] = (uint32_t)(counts[1] * 30 * 1000 / RPM_READ_INTERVAL_MS);
    this->lastRPM[2] = (uint32_t)(counts[2] * 30 * 1000 / RPM_READ_INTERVAL_MS);
    this->displayDriver->setRPM(this->lastRPM[0], this->lastRPM[1], this->lastRPM[2]);

    // Accumulate all pulse counts into the filter usage counter
    this->filterUsageCounter += (uint64_t)(counts[0] + counts[1] + counts[2]);
    this->displayDriver->setFilterUsage(this->filterUsageCounter);

    // Persist to NVS periodically
    if (++this->nvsFlushCounter >= NVS_FLUSH_INTERVAL)
    {
        this->nvsFlushCounter = 0;
        this->saveFilterCounter();
    }
}

void FanDriver::saveFilterCounter()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("app_data", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        nvs_set_u64(nvs, "filter_cnt", this->filterUsageCounter);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Filter usage counter saved: %llu", (unsigned long long)this->filterUsageCounter);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to open NVS for filter counter save: %d", err);
    }
}

void FanDriver::resetFilterCounter()
{
    this->filterUsageCounter = 0;
    this->nvsFlushCounter = 0;
    this->saveFilterCounter();
    this->displayDriver->setFilterUsage(0);
    ESP_LOGI(TAG, "Filter usage counter reset");
}

// ── Attribute update callback (Matter only) ───────────────────────────────────

#ifndef CONFIG_STANDALONE_MODE
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

    if (this->updatingAttributesInCallback)
        return ESP_OK;

    if (attribute_id == FanControl::Attributes::FanMode::Id)
    {
        // Writing On is deprecated — treat as High (spec §4.4.6.1).
        // Patch val so the framework stores kHigh, not kOn.
        if (val->val.u8 == (uint8_t)FanControl::FanModeEnum::kOn)
            val->val.u8 = (uint8_t)FanControl::FanModeEnum::kHigh;
        uint8_t newMode = val->val.u8;

        ESP_LOGI(TAG, "FanMode -> %u", newMode);
        this->setFanPercentSetting(fanModeDefaultPercent(newMode));

        this->updatingAttributesInCallback = true;
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, this->fanPercentSetting);
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttributesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::PercentSetting::Id)
    {
        const uint8_t newPct = val->val.u8;
        if (newPct == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.3)

        ESP_LOGI(TAG, "PercentSetting -> %u", newPct);
        this->setFanPercentSetting(newPct);

        this->updatingAttributesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(this->fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(this->fanPercentSetting));
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttributesInCallback = false;
    }
    else if (attribute_id == FanControl::Attributes::SpeedSetting::Id)
    {
        const uint8_t newSpeed = val->val.u8;
        if (newSpeed == 0xFF)
            return ESP_OK;   // null write → SHALL NOT change (spec §4.4.6.6)

        ESP_LOGI(TAG, "SpeedSetting -> %u", newSpeed);
        this->setFanPercentSetting(newSpeed);

        this->updatingAttributesInCallback = true;
        // Map percent to FanMode and keep SpeedSetting in sync
        updateAttrEnum8(this->fanEndpointId, FC, FanControl::Attributes::FanMode::Id, percentToFanMode(this->fanPercentSetting));
        updateAttrNullableU8(this->fanEndpointId, FC, FanControl::Attributes::PercentSetting::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::PercentCurrent::Id, this->fanPercentSetting);
        updateAttrU8(this->fanEndpointId, FC, FanControl::Attributes::SpeedCurrent::Id, this->fanPercentSetting);
        this->updatingAttributesInCallback = false;
    }
    else
    {
        return ESP_OK;  // Not a handled attribute
    }

    return ESP_OK;
}
#endif // !CONFIG_STANDALONE_MODE

// ── Apply hardware state ──────────────────────────────────────────────────────

void FanDriver::applyFanState()
{
    gpio_set_level(FAN_POWER_GPIO, this->fanPercentSetting > 0 ? 1 : 0);

    const uint32_t duty = (static_cast<uint32_t>(this->fanPercentSetting) * PWM_MAX_DUTY) / 100;
    ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
    ESP_LOGI(TAG, "Fan PWM: %u%% (duty %lu/%lu)", this->fanPercentSetting, duty, PWM_MAX_DUTY);
}

