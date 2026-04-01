#include <algorithm>
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
    this->setFanPercentSetting(0);
}

void DisplayDriver::setFanPercentSetting(uint8_t newSetting)
{
    if (this->fanPercentSetting == newSetting)
        return;
    this->fanPercentSetting = newSetting;
    // set the animation speed and screen update rate based on fanPercentSetting
    static constexpr uint8_t animationTableCount = 8;
    static uint8_t animationFrameSpeeds[animationTableCount] = {0, 1,  1,  2,  2,  3,  3,  3};
    static uint8_t animationTaskDelays[animationTableCount] =  {8, 4,  3,  4,  3,  4,  3,  2};
    static uint8_t animationIntervals[animationTableCount] =   {0, 10, 20, 30, 40, 60, 90, 100};
    uint8_t animationTableIndex = animationTableCount - 1;
    for (uint8_t i = 0; i < animationTableCount; i++)
    {
        if (animationIntervals[i] >= this->fanPercentSetting)
        {
            animationTableIndex = i;
            break;
        }
    }
    this->animationFrameSpeed = animationFrameSpeeds[animationTableIndex];
    this->screenUpdateTaskDelay = animationTaskDelays[animationTableIndex];
    
    this->mainScreenDirty = true;
}

void DisplayDriver::setRPM(uint32_t fan1RPM, uint32_t fan2RPM, uint32_t fan3RPM)
{
    this->fan1RPM = fan1RPM;
    this->fan2RPM = fan2RPM;
    this->fan3RPM = fan3RPM;
    uint32_t minRPM = std::min({this->fan1RPM, this->fan2RPM, this->fan3RPM});
    if (this->mainScreenRPM != minRPM)
    {
        this->mainScreenRPM = minRPM;
        this->mainScreenRPMCountDirty = true;
    }
    this->infoScreenDirty = true;
}

void DisplayDriver::setActiveScreen(Screen screen)
{
    this->activeScreen = screen;
    if (screen == Screen::Main)
        this->mainScreenDirty = true;
    else if (screen == Screen::Identify)
        this->identifyScreenDirty = true;
    else if (screen == Screen::FactoryReset)
        this->factoryResetScreenDirty = true;
    else if (screen == Screen::Info)
        this->infoScreenDirty = true;
}

void DisplayDriver::setSignal(int8_t rssi)
{
    this->signalRSSI = rssi;
    uint8_t activeSignalBars;
    if      (rssi >= -65) activeSignalBars = 4;
    else if (rssi >= -75) activeSignalBars = 3;
    else if (rssi >= -85) activeSignalBars = 2;
    else if (rssi >= -95) activeSignalBars = 1;
    else                  activeSignalBars = 0;
    if (this->mainScreenActiveSignalBars != activeSignalBars)
    {
        this->mainScreenActiveSignalBars = activeSignalBars;
        this->mainScreenSignalBarsDirty = true;
    }
    this->infoScreenDirty = true;
}

void DisplayDriver::setThreadRole(ThreadRole role)
{
    if (this->threadRole != role)
    {
        this->threadRole = role;
        this->infoScreenDirty = true;
    }
}

void DisplayDriver::drawSplashScreen()
{
    u8g2_SetFont(&this->display, u8g2_font_t0_13_tr);
    u8g2_DrawStr(&this->display, 38, 14, "Air purifier");

    u8g2_SetFont(&this->display, u8g2_font_5x8_tr);
    const esp_app_desc_t* app_desc = esp_app_get_description();
    char versionString[sizeof(app_desc->version) + 1];
    versionString[0] = 'v';
    strlcpy(versionString + 1, app_desc->version, sizeof(app_desc->version));
    u8g2_DrawStr(&this->display, 67, 27, versionString);
    u8g2_DrawXBM(&this->display, 2, 3, AIR_PURIFIER_ICON_WIDTH, AIR_PURIFIER_ICON_HEIGH, AIR_PURIFIER_ICON);
    u8g2_SendBuffer(&this->display);
}

void DisplayDriver::drawMainScreen()
{
    if (this->mainScreenDirty)
    {
        // clear only my area, leave animation
        u8g2_SetDrawColor(&this->display, 0);
        u8g2_DrawBox(&this->display, 32, 0, 128 - 32, 32);
        u8g2_SetDrawColor(&this->display, 1);

        //progress bar
        u8g2_DrawFrame(&this->display, 34, 18, 69, 14);
        uint8_t barWidth = this->fanPercentSetting * 67 / 100;
        u8g2_DrawBox(&this->display, 35, 19, barWidth, 12);

        //percent
        u8g2_SetFont(&this->display, u8g2_font_6x13_tr);
        char s[5];
        snprintf(s, sizeof(s), "%d%%", this->fanPercentSetting);
        u8g2_DrawStr(&this->display, 127 - u8g2_GetStrWidth(&this->display, s), 30, s);

        //signal
        this->drawMainScreenSignalBars();

        //RPM
        this->drawMainScreenRPMCount();
        u8g2_DrawStr(&this->display, 63, 13, "RPM");

        if (this->animationFrameSpeed == 0)
            this->drawMainScreenAnimation(true);

        // update only right area (tile 4 onward), leave animation (tiles 0–3)
        u8g2_UpdateDisplayArea(&this->display, 4, 0, 12, 4);
        this->mainScreenDirty = false;
    }
    else
    {
        if (this->mainScreenRPMCountDirty)
        {
            this->drawMainScreenRPMCount();
        }
        if (this->mainScreenSignalBarsDirty)
        {
            this->drawMainScreenSignalBars();
        }
    }

    this->drawMainScreenAnimation();

}

void DisplayDriver::drawMainScreenSignalBars()
{
    if (this->activeScreen != Screen::Main)
        return;

    u8g2_SetDrawColor(&this->display, 0);
    u8g2_DrawBox(&this->display, 112, 0, 16, 15);
    u8g2_SetDrawColor(&this->display, 1);

    for (int i = 0; i < 4; i++) 
    {
        bool filled = this->mainScreenActiveSignalBars >= i + 1;
        (filled ? u8g2_DrawBox : u8g2_DrawFrame)(&this->display, 112 + i * 4, 12 - i * 4, 3, 4 * i + 3);
    }
    u8g2_UpdateDisplayArea(&this->display, 14, 0, 2, 2);
    this->mainScreenSignalBarsDirty = false;
}

void DisplayDriver::drawMainScreenRPMCount()
{
    if (this->activeScreen != Screen::Main)
        return;
    u8g2_SetDrawColor(&this->display, 0);
    u8g2_DrawBox(&this->display, 37, 0, 21, 13);
    u8g2_SetDrawColor(&this->display, 1);
    u8g2_SetFont(&this->display, u8g2_font_6x13_tr);
    char s[6];
    snprintf(s, sizeof(s), "%5lu", (unsigned long)this->mainScreenRPM);
    u8g2_DrawStr(&this->display, 37, 13, s);
    u8g2_UpdateDisplayArea(&this->display, 4, 0, 4, 2);
    this->mainScreenRPMCountDirty = false;
}

void DisplayDriver::invertDisplayBuffer()
{
    uint8_t *buf = u8g2_GetBufferPtr(&this->display);
    uint16_t len = u8g2_GetBufferTileWidth(&this->display) * u8g2_GetBufferTileHeight(&this->display) * 8;
    for (uint16_t i = 0; i < len; i++)
        buf[i] ^= 0xFF;
    u8g2_SendBuffer(&this->display);
}

void DisplayDriver::drawIdentifyScreen()
{
    TickType_t now = xTaskGetTickCount();

    if (this->identifyScreenDirty)
    {
        u8g2_ClearBuffer(&this->display);

        u8g2_DrawXBM(&this->display, 0, 0, 32, 32, WARNING_ICON);
        u8g2_SetFont(&this->display, u8g2_font_t0_17b_tr);
        u8g2_DrawStr(&this->display, 44, 23, "Identify");

        u8g2_SendBuffer(&this->display);

        this->identifyScreenDirty = false;
        this->identifyStartTick = now;
        this->identifyLastInvertTick = now;
        return;
    }

    if ((now - this->identifyStartTick) >= pdMS_TO_TICKS(10000))
    {
        this->setActiveScreen(Screen::Main);
        return;
    }

    if ((now - this->identifyLastInvertTick) >= pdMS_TO_TICKS(500))
    {
        this->invertDisplayBuffer();
        this->identifyLastInvertTick = now;
    }
}

void DisplayDriver::drawFactoryResetScreen()
{
    if (this->factoryResetScreenDirty)
    {
        u8g2_ClearBuffer(&this->display);

        u8g2_DrawXBM(&this->display, 0, 0, 32, 32, WARNING_ICON);
        u8g2_SetFont(&this->display, u8g2_font_6x13_tr);
        u8g2_DrawStr(&this->display, 40, 14, "Factory Reset");

        u8g2_SetFont(&this->display, u8g2_font_4x6_tr);
        u8g2_DrawStr(&this->display, 35, 25, "Factory reset triggered.");
        u8g2_DrawStr(&this->display, 35, 31, "Release to start reset.");

        u8g2_SendBuffer(&this->display);

        this->factoryResetScreenDirty = false;
        return;
    }
}

void DisplayDriver::drawInfoScreen()
{
    if (!this->infoScreenDirty)
        return;

    u8g2_ClearBuffer(&this->display);
    u8g2_SetFont(&this->display, u8g2_font_profont10_tr);

    char line[38];

    // Fan RPMs
    snprintf(line, sizeof(line), "%4lu|%4lu|%4lu RPM",
             (unsigned long)this->fan1RPM,
             (unsigned long)this->fan2RPM,
             (unsigned long)this->fan3RPM);
    u8g2_DrawStr(&this->display, 1, 7, line);

    // Signal strength
    snprintf(line, sizeof(line), "Sig: %4d dBm", (int)this->signalRSSI);
    u8g2_DrawStr(&this->display, 1, 15, line);

    // Thread role
    const char *roleStr;
    switch (this->threadRole)
    {
        case ThreadRole::Child:    roleStr = "Child";    break;
        case ThreadRole::Router:   roleStr = "Router";   break;
        case ThreadRole::Leader:   roleStr = "Leader";   break;
        case ThreadRole::Detached: roleStr = "Detached"; break;
        default:                   roleStr = "Disabled"; break;
    }
    snprintf(line, sizeof(line), "Role: %s", roleStr);
    u8g2_DrawStr(&this->display, 1, 23, line);

    // Firmware version
    const esp_app_desc_t *app_desc = esp_app_get_description();
    snprintf(line, sizeof(line), "fw: v%s", app_desc->version);
    u8g2_DrawStr(&this->display, 1, 31, line);

    u8g2_SendBuffer(&this->display);
    this->infoScreenDirty = false;
}

void DisplayDriver::drawMainScreenAnimation(bool force)
{
    if (this->animationFrameSpeed == 0 && !force)
        return;
    u8g2_DrawXBM(&this->display, 0, 0, 32, 32, FAN_ANIMATION[this->currentAnimationFrame]);
    this->currentAnimationFrame = this->currentAnimationFrame + this->animationFrameSpeed;
    if (this->currentAnimationFrame >= FAN_ANIMATION_FRAMES_COUNT)
        this->currentAnimationFrame = this->currentAnimationFrame % FAN_ANIMATION_FRAMES_COUNT;
    u8g2_UpdateDisplayArea(&this->display, 0, 0, 4, 4);
}


void DisplayDriver::startTask()
{
    xTaskCreate(screenUpdateTask, "display_update", 4096, this, 5, nullptr);
}

void DisplayDriver::screenUpdateTask(void *arg)
{
    DisplayDriver *driver = static_cast<DisplayDriver *>(arg);
    while (true)
    {
        if (driver->activeScreen == Screen::Main)
        {
            driver->drawMainScreen();
        }
        else if (driver->activeScreen == Screen::Identify)
        {
            driver->drawIdentifyScreen();
        }
        else if (driver->activeScreen == Screen::FactoryReset)
        {
            driver->drawFactoryResetScreen();
        }
        else if (driver->activeScreen == Screen::Info)
        {
            driver->drawInfoScreen();
        }
        vTaskDelay(driver->screenUpdateTaskDelay);
    }
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