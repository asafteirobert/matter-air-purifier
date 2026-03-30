#pragma once
#include "driver/gpio.h"

/** Status LED – lights when the fan is ON */
#define FAN_LED_GPIO    GPIO_NUM_15

/** on/off / Factory-reset button (active-LOW, internal pull-up) */
#define BOARD_BUTTON_GPIO     GPIO_NUM_9

/** interaction button (active-LOW, internal pull-up) */
#define PANEL_BUTTON_GPIO     GPIO_NUM_23

/** PC fan PWM */
#define FAN_PWM_GPIO     GPIO_NUM_2

/** RF switch power – must be driven LOW to enable antenna switching */
#define ANTENNA_ENABLE_GPIO     GPIO_NUM_3

/** RF switch select – LOW = internal ceramic antenna, HIGH = external antenna */
#define ANTENNA_CONFIG_GPIO GPIO_NUM_14

/** OLED screen SDA Pin*/
#define OLED_SDA_GPIO GPIO_NUM_22

/** OLED screen SCL Pin*/
#define OLED_SCL_GPIO GPIO_NUM_21



#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
