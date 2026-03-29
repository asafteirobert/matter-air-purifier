#include "sdkconfig.h"
#include "CliCommands.hpp"

#if CONFIG_ENABLE_CHIP_SHELL

#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <cstring>

#include "Constants.hpp"

// ── antenna ──────────────────────────────────────────────────────────────────

static struct
{
    struct arg_str *type;
    struct arg_end *end;
} antenna_args;

static int antenna_cmd_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&antenna_args);
    if (nerrors != 0) 
    {
        arg_print_errors(stderr, antenna_args.end, argv[0]);
        return 1;
    }

    const char *type = antenna_args.type->sval[0];
    bool use_external = false;
    bool rf_switch_on = false;
    if (strcmp(type, "external") == 0) 
    {
        rf_switch_on = true;
        use_external = true;
    } else if (strcmp(type, "internal") == 0) 
    {
        rf_switch_on = true;
        use_external = false;
    } else if (strcmp(type, "floating") == 0) 
    {
        rf_switch_on = false;
        use_external = false;
    } else 
    {
        printf("Invalid antenna type '%s'. Use 'internal', 'external' or 'floating'.\n", type);
        return 1;
    }

    // Activate RF switch
    gpio_set_direction(ANTENNA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_ENABLE_GPIO, rf_switch_on ? 0 : 1); //active low

    // Select antenna
    gpio_set_direction(ANTENNA_CONFIG_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ANTENNA_CONFIG_GPIO, use_external ? 1 : 0);

    printf("Switched to %s antenna\n", type);
    return 0;
}

static void antenna_register_commands()
{
    antenna_args.type = arg_str1(NULL, NULL, "<internal|external|floating>", "Antenna type to use");
    antenna_args.end  = arg_end(1);

    const esp_console_cmd_t cmd = 
    {
        .command  = "antenna",
        .help     = "Switch between internal or external antenna. Or turn off RF switch",
        .hint     = NULL,
        .func     = &antenna_cmd_handler,
        .argtable = &antenna_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ── public entry point ───────────────────────────────────────────────────────

void cli_register_commands()
{
    antenna_register_commands();
}

#endif // CONFIG_ENABLE_CHIP_SHELL