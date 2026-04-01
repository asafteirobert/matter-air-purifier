#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include "CliCommands.hpp"

#include "ButtonDriver.hpp"
#include "FanDriver.hpp"
#include "DisplayDriver.hpp"

#include "Utils.hpp"

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/thread.h>
#endif

#include "esp_timer.h"

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "app_main";
constexpr auto k_timeout_seconds = 300;

ButtonDriver buttonDriver;
FanDriver fanDriver;
DisplayDriver displayDriver;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) 
    {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: 
    {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) 
        {
            chip::CommissioningWindowManager  &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen())
            {
                /* After removing last fabric, we do not remove the Wi-Fi credentials
                 * and still has IP connectivity so, only advertising on DNS-SD.
                 */
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                                            chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR)
                {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    if (type == identification::START || type == identification::EFFECT)
    {
        displayDriver.setActiveScreen(DisplayDriver::Screen::Identify);
    }
    return ESP_OK;
}

/**
 * Called by the Matter stack BEFORE (PRE_UPDATE) and AFTER (POST_UPDATE) an
 * attribute is written.  We only need PRE_UPDATE to drive the hardware.
 */
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t  endpoint_id,
                                         uint32_t  cluster_id,
                                         uint32_t  attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void *priv_data)
{
    //ESP_LOGI(TAG, "app_attribute_update_cb type: %d, endpoint_id: %d, cluster_id: %d, attribute_id: %d, val.u8: %d", 
    //              type,
    //              endpoint_id, 
    //              cluster_id, 
    //              attribute_id, 
    //              (val) ? val->val.u8 : -1);

    return fanDriver.attributeUpdate(type, endpoint_id, cluster_id, attribute_id, val);
}

static void signalTimerCb(void * /*arg*/)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(100)))
    {
        otInstance   *instance = esp_openthread_get_instance();
        otDeviceRole  role     = otThreadGetDeviceRole(instance);
        int8_t        rssi     = -120;
        bool          valid    = false;

        if (role == OT_DEVICE_ROLE_CHILD)
        {
            valid = (otThreadGetParentAverageRssi(instance, &rssi) == OT_ERROR_NONE);
        }
        else if (role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER)
        {
            otNeighborInfoIterator it = OT_NEIGHBOR_INFO_ITERATOR_INIT;
            otNeighborInfo         info;
            while (otThreadGetNextNeighborInfo(instance, &it, &info) == OT_ERROR_NONE)
            {
                if (!info.mIsChild && info.mAverageRssi > rssi)
                    rssi = info.mAverageRssi;
            }
            valid = (rssi > -120);
        }

        esp_openthread_lock_release();

        //if (valid)
        //    ESP_LOGI(TAG, "Thread RSSI: %d dBm (role %d)", rssi, (int)role);
        //else
        //    ESP_LOGW(TAG, "Thread RSSI unavailable (role %d)", (int)role);

        DisplayDriver::ThreadRole displayRole;
        switch (role)
        {
            case OT_DEVICE_ROLE_CHILD:    displayRole = DisplayDriver::ThreadRole::Child;    break;
            case OT_DEVICE_ROLE_ROUTER:   displayRole = DisplayDriver::ThreadRole::Router;   break;
            case OT_DEVICE_ROLE_LEADER:   displayRole = DisplayDriver::ThreadRole::Leader;   break;
            case OT_DEVICE_ROLE_DETACHED: displayRole = DisplayDriver::ThreadRole::Detached; break;
            default:                      displayRole = DisplayDriver::ThreadRole::Disabled; break;
        }
        displayDriver.setThreadRole(displayRole);
        displayDriver.setSignal(rssi);
    }
#endif
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    // Initialize the ESP NVS layer
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Switch to internal antenna
    // Activate RF switch
    gpio_set_direction(ANTENNA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_ENABLE_GPIO, 0); //active low

    // Select internal antenna
    gpio_set_direction(ANTENNA_CONFIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_CONFIG_GPIO, 0);

    displayDriver.init();
    displayDriver.drawSplashScreen();

    // Create a Matter node and add the mandatory Root Node device type on endpoint 0
    node::config_t node_config;

    // node handle can be used to add/modify other endpoints.
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    air_purifier::config_t air_purifier_config;
    air_purifier_config.fan_control.fan_mode          = (uint8_t)FanControl::FanModeEnum::kOff;
    air_purifier_config.fan_control.fan_mode_sequence = (uint8_t)FanControl::FanModeSequenceEnum::kOffLowMedHigh;

    endpoint_t *air_purifier_endpoint = air_purifier::create(node, &air_purifier_config, ENDPOINT_FLAG_NONE, nullptr);
    if (air_purifier_endpoint == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create air purifier endpoint");
        return;
    }

    // Enable the MultiSpeed feature (required by Home Assistant for percentage control).
    // SpeedMax=100 makes SpeedSetting a direct 0-100 percentage.
    cluster_t *fan_cluster = cluster::get(air_purifier_endpoint, chip::app::Clusters::FanControl::Id);
    cluster::fan_control::feature::multi_speed::config_t multispeed_config;
    multispeed_config.speed_max     = 100;
    multispeed_config.speed_setting = nullable<uint8_t>();   // null – no speed set yet
    multispeed_config.speed_current = 0;
    ABORT_APP_ON_FAILURE(cluster::fan_control::feature::multi_speed::add(fan_cluster, &multispeed_config) == ESP_OK,
                         ESP_LOGE(TAG, "Failed to add MultiSpeed feature"));

    uint16_t fanEndpointId = endpoint::get_id(air_purifier_endpoint);
    ESP_LOGI(TAG, "Air purifier endpoint created with endpoint id: %u", fanEndpointId);

    // Initialize drivers
    buttonDriver.init(fanEndpointId, displayDriver);
    fanDriver.init(fanEndpointId, displayDriver);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create secondary network interface endpoint"));
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = 
    {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    ESP_LOGI(TAG, "Matter started");
    fanDriver.syncFromMatter();

    esp_timer_handle_t signalTimer = nullptr;
    const esp_timer_create_args_t signalTimerArgs = {
        .callback = signalTimerCb,
        .arg      = nullptr,
        .name     = "signal_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&signalTimerArgs, &signalTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(signalTimer, 10 * 1000 * 1000 /* µs = 10 s */));
    signalTimerCb(nullptr);


#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    cli_register_commands();
    esp_matter::console::init();
#endif

    ESP_LOGI(TAG, "Init finished");

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    displayDriver.startTask();
    vTaskDelete(nullptr);
}
