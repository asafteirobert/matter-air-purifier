#include <esp_log.h>
#include "DisplayDriver.hpp"
#include "DisplayIcons.hpp"
#include "Constants.hpp"
#include "esp_app_desc.h"


void DisplayDriver::init()
{
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = OLED_SDA_GPIO;
    u8g2_esp32_hal.bus.i2c.scl = OLED_SCL_GPIO;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_ssd1306_i2c_128x32_univision_f(&this->display, U8G2_R0,
                                              // u8x8_byte_sw_i2c,
                                              u8g2_esp32_i2c_byte_cb,
                                              u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure
    u8x8_SetI2CAddress(&this->display.u8x8, 0x78);

    u8g2_InitDisplay(&this->display);  // send init sequence to the display, display is in sleep mode after this
    u8g2_SetPowerSave(&this->display, 0);  // wake up display
    u8g2_SetBitmapMode(&this->display, 0);
    u8g2_SetFontMode(&this->display, 0);
    u8g2_ClearBuffer(&this->display);
    u8g2_SendBuffer(&this->display);
    ESP_LOGI(TAG, "u8g2 display initialized");
}

void DisplayDriver::drawSplashScreen()
{
    static const uint8_t airPurifierIcon[] = {0xff,0xff,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x01,0x01,0x00,0x81,0x02,0x01,0x00,0x01,0x01,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0xd9,0x36,0x01,0x00,0xd9,0x36,0x01,0x00,0xd9,0x36,0x8d,0x03,0xd9,0x36,0xd9,0x06,0xd9,0x36,0x71,0x0c,0xd9,0x36,0x01,0x00,0xd9,0x36,0x01,0x00,0xd9,0x36,0x8d,0x03,0xd9,0x36,0xd9,0x06,0xd9,0x36,0x71,0x0c,0xd9,0x36,0x01,0x00,0x01,0x00,0x01,0x00,0xf9,0x3f,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,0xff,0xff,0x01,0x00,0x24,0x48,0x00,0x00,0x3c,0x78,0x00,0x00};

    u8g2_SetFont(&this->display, u8g2_font_t0_13_tr);
    u8g2_DrawStr(&this->display, 38, 14, "Air purifier");

    u8g2_SetFont(&this->display, u8g2_font_5x8_tr);
    const esp_app_desc_t* app_desc = esp_app_get_description();
    char versionString[sizeof(app_desc->version) + 1];
    versionString[0] = 'v';
    strlcpy(versionString + 1, app_desc->version, sizeof(app_desc->version));
    u8g2_DrawStr(&this->display, 67, 27, versionString);
    u8g2_DrawXBM(&this->display, 2, 3, 28, 25, airPurifierIcon);
    u8g2_SendBuffer(&this->display);
}

void DisplayDriver::drawMainScreen()
{
    u8g2_ClearBuffer(&this->display);
    //progress bar
    u8g2_DrawFrame(&this->display, 34, 18, 69, 14);
    u8g2_DrawBox(&this->display, 35, 19, 17, 12);

    //percent
    u8g2_SetFont(&this->display, u8g2_font_6x13_tr);
    u8g2_DrawStr(&this->display, 110, 30, "23%");

    //signal
    u8g2_DrawFrame(&this->display, 112, 12, 3, 3);
    u8g2_DrawFrame(&this->display, 116, 8, 3, 7);
    u8g2_DrawFrame(&this->display, 120, 4, 3, 11);
    u8g2_DrawFrame(&this->display, 124, 0, 3, 15);


    //RPM
    u8g2_DrawStr(&this->display, 37, 13, "9999");
    u8g2_DrawStr(&this->display, 63, 13, "RPM");

    u8g2_SendBuffer(&this->display); 
}

void DisplayDriver::drawIdentifyScreen()
{
    u8g2_ClearBuffer(&this->display);

    u8g2_DrawXBM(&this->display, 0, 0, 32, 32, warningIcon);
    u8g2_SetFont(&this->display, u8g2_font_t0_17b_tr);
    u8g2_DrawStr(&this->display, 44, 23, "Identify");

    //Todo, invert buffer

    u8g2_SendBuffer(&this->display); 
}

void DisplayDriver::drawInfoScreen()
{
    u8g2_ClearBuffer(&this->display);

    u8g2_SetFont(&this->display, u8g2_font_profont10_tr);
    u8g2_DrawStr(&this->display, 1, 7, "Info 1: 192.168.10.10");
    u8g2_DrawStr(&this->display, 1, 15, "Info 2: Connected");
    u8g2_DrawStr(&this->display, 1, 23, "Info 3: Hello World");
    u8g2_DrawStr(&this->display, 1, 31, "Speeds: 1234 3214 900");

    u8g2_SendBuffer(&this->display); 
}

void DisplayDriver::drawAnimation()
{
    /* 
    Animation speed table
    Frames	Delay	Interval start	interval end
    1	    4	    0	             0.1
    1	    3	    0.1	             0.2
    2	    4	    0.2	             0.3
    2	    3	    0.3	             0.4
    3	    4	    0.4	             0.6
    3	    3	    0.6	             0.9
    3	    2	    0.9	             1

    Frames = how many frames to increment per draw
    Delay = task delay
    Interval = when to use this speed based on speed percent.
    */

    static int frame = 0;
    // Set draw color to 0 (black = erase)
    //u8g2_SetDrawColor(&this->display, 0);
    //u8g2_DrawBox(&this->display, 0, 0, 32, 32);
    //u8g2_SetDrawColor(&this->display, 1);

    //u8g2_ClearBuffer(&this->display);
    u8g2_DrawXBM(&this->display, 0, 0, 32, 32, fanAanimation[frame]);
    frame = frame + 3;
    if (frame >= 18)
        frame = 0;
    //this->sendPartialBuffer(&this->display, 0, 3, 0, 31);
    //u8g2_SendBuffer(&this->display);
    u8g2_UpdateDisplayArea(&this->display, 0, 0, 4, 4);
}


void DisplayDriver::sendPartialBuffer(u8g2_t *u8g2,
                                      uint8_t page_start, uint8_t page_end,
                                      uint8_t col_start,  uint8_t col_end)
{
    u8x8_t *u8x8 = &u8g2->u8x8;
    uint16_t width = col_end - col_start + 1;

    // Single transaction — one alloc, one free
    u8x8_cad_StartTransfer(u8x8);

    // Set column address range
    u8x8_cad_SendCmd(u8x8, 0x21);
    u8x8_cad_SendArg(u8x8, col_start);
    u8x8_cad_SendArg(u8x8, col_end);

    // Set page address range
    u8x8_cad_SendCmd(u8x8, 0x22);
    u8x8_cad_SendArg(u8x8, page_start);
    u8x8_cad_SendArg(u8x8, page_end);

    // Send all pages without re-opening the command link
    for (uint8_t page = page_start; page <= page_end; page++) {
        uint8_t *buf = u8g2_GetBufferPtr(u8g2)
                       + (page * u8g2_GetBufferTileWidth(u8g2) * 8)
                       + col_start;
        u8x8_cad_SendData(u8x8, width, buf);
    }

    u8x8_cad_EndTransfer(u8x8);  // executes and frees the command link once
}