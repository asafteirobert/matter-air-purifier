#include "FanDriver.hpp"

#include <esp_log.h>
#include <nvs.h>

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

// ── Apply hardware state ──────────────────────────────────────────────────────

void FanDriver::applyFanState()
{
    gpio_set_level(FAN_POWER_GPIO, this->fanPercentSetting > 0 ? 1 : 0);

    const uint32_t duty = (static_cast<uint32_t>(this->fanPercentSetting) * PWM_MAX_DUTY) / 100;
    ledc_set_duty(PWM_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_SPEED_MODE, PWM_CHANNEL);
    ESP_LOGI(TAG, "Fan PWM: %u%% (duty %lu/%lu)", this->fanPercentSetting, duty, PWM_MAX_DUTY);
}
