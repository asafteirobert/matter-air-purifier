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
            Info,
            Comission
        };

        enum class ThreadRole : uint8_t { Disabled = 0, Detached, Child, Router, Leader };

        void init();
        void startTask();
        void setFanPercentSetting(uint8_t newSetting);
        void setRPM(uint32_t fan1RPM, uint32_t fan2RPM, uint32_t fan3RPM);
        void setActiveScreen(Screen screen);
        void setSignal(int8_t rssi);
        void setThreadRole(ThreadRole role);
        void setFilterUsage(uint64_t counter);
        void drawSplashScreen();
        void setCommissioningQRCode(const char *payload);
        void setCommissioningManualCode(const char *code);
        Screen getActiveScreen() const { return this->activeScreen; }

    private:
        static void screenUpdateTask(void *arg);
        void drawMainScreen();
        void drawMainScreenSignalBars();
        void drawMainScreenRPMCount();
        void drawMainScreenAnimation(bool force = false);
        void drawIdentifyScreen();
        void invertDisplayBuffer();
        void drawFactoryResetScreen();
        void drawComissionScreen();
        void drawInfoScreen();

        void drawQrCode();

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
        bool comissionScreenDirty = true;
        bool infoScreenDirty = true;
        int8_t signalRSSI = -120;
        ThreadRole threadRole = ThreadRole::Disabled;
        uint64_t filterUsageCounter = 0;
        // QR code bitmap buffer — fits up to version 3 (29x29 modules)
        static constexpr size_t QR_CODE_BUFFER_SIZE = 107;
        uint8_t qrCodeBuffer[QR_CODE_BUFFER_SIZE] = {};
        int qrCodeSize = 0;
        bool hasQRCode = false;
        char manualCode[32] = {};
        bool hasManualCode = false;
        u8g2_t display;
};
