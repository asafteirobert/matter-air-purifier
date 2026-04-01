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

    enum class Screen
    {
        Main = 0,
        Identify,
        FactoryReset,
        Info
    };

    public:
        void init();
        void setFanPercentSetting(uint8_t newSetting);
        void setRPM(uint32_t fan1RPM, uint32_t fan2RPM, uint32_t fan3RPM);
        void setActiveScreen(Screen screen);
        void setSignal(int8_t rssi);
        void drawSplashScreen();
        void drawMainScreen();
        void drawMainScreenSignalBars();
        void drawMainScreenRPMCount();
        void drawIdentifyScreen();
        void drawFactoryResetScreen();
        void drawInfoScreen();

        void drawAnimation();

    private:
        void sendPartialBuffer(u8g2_t *u8g2, uint8_t page_start, uint8_t page_end, uint8_t col_start, uint8_t col_end);
        uint8_t fanPercentSetting = 0;
        uint32_t fan1RPM = 0;
        uint32_t fan2RPM = 0;
        uint32_t fan3RPM = 0;
        uint32_t mainScreenRPM = 0;
        uint8_t mainScreenActiveSignalBars = 0;
        Screen activeScreen = Screen::Main;
        bool mainScreenDirty = true;
        bool mainScreenRPMCountDirty = false;
        bool mainScreenSignalBarsDirty = false;
        uint8_t currentAnimationFrame = 0;
        uint8_t animationFrameSpeed = 1;
        uint8_t animationTaskDelay = 1;
        u8g2_t display;
};