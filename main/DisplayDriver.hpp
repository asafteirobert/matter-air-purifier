#pragma once
#include <u8g2.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C"
{
    #include "u8g2_esp32_hal.h"
}

class DisplayDriver
{
    static constexpr const char *TAG = "app_display_driver";

    public:
        enum class Screen
        {
            Main = 0,
            Identify,
            FactoryReset,
            Info
        };

        enum class ThreadRole : uint8_t { Disabled = 0, Detached, Child, Router, Leader };

        void init();
        void startTask();
        void setFanPercentSetting(uint8_t newSetting);
        void setRPM(uint32_t fan1RPM, uint32_t fan2RPM, uint32_t fan3RPM);
        void setActiveScreen(Screen screen);
        void setSignal(int8_t rssi);
        void setThreadRole(ThreadRole role);
        void drawSplashScreen();
        Screen getActiveScreen() const { return this->activeScreen; }

    private:
        static void screenUpdateTask(void *arg);
        void sendPartialBuffer(u8g2_t *u8g2, uint8_t page_start, uint8_t page_end, uint8_t col_start, uint8_t col_end);
        void drawMainScreen();
        void drawMainScreenSignalBars();
        void drawMainScreenRPMCount();
        void drawMainScreenAnimation(bool force = false);
        void drawIdentifyScreen();
        void invertDisplayBuffer();
        void drawFactoryResetScreen();
        void drawInfoScreen();

        uint8_t fanPercentSetting = 255;
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
        uint8_t screenUpdateTaskDelay = 1;
        bool identifyScreenDirty = true;
        TickType_t identifyStartTick = 0;
        TickType_t identifyLastInvertTick = 0;
        bool factoryResetScreenDirty = true;
        bool infoScreenDirty = true;
        int8_t signalRSSI = -120;
        ThreadRole threadRole = ThreadRole::Disabled;
        u8g2_t display;
};
