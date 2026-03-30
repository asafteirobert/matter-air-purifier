#pragma once
#include <u8g2.h>
#include "sdkconfig.h"
extern "C"
{
    #include "u8g2_esp32_hal.h"
}


class DisplayDriver
{
    static constexpr char *TAG = "app_display_driver";
    public:
        void init();
        void drawSplashScreen();
        void drawMainScreen();
        void drawIdentifyScreen();
        void drawInfoScreen();
        void drawAnimation();

    private:
        void sendPartialBuffer(u8g2_t *u8g2, uint8_t page_start, uint8_t page_end, uint8_t col_start, uint8_t col_end);
        u8g2_t display;
};