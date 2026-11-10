// EthAir: makes an ESP32-S3 look like a USB Ethernet adapter to any device
// with a USB host port (e.g. a camera) and bridges that link to its own Wi-Fi
// access point.

#include <cstring>

#include "boot_mode.h"
#include "bridge.h"
#include "button.h"
#include "dhcp_task.h"
#include "display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_buffer.h"
#include "logic/boot_guard.h"
#include "mdns_task.h"
#include "net_config.h"
#include "nvs_flash.h"
#include "settings_store.h"
#include "web.h"

namespace {
const char* TAG = "main";
constexpr int64_t OTA_CONFIRM_MS = 30000;

void initNvs() {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
}

void addResetHint(DisplayModel& m) {
    const uint32_t held = buttonHeldMs();
    if (held >= 500) {
        const uint32_t left = held >= FACTORY_RESET_HOLD_MS ? 0 : (FACTORY_RESET_HOLD_MS - held + 999) / 1000;
        displaySetLine(m, 6, COLOR_RED, "Factory reset in %lus", (unsigned long)left);
    } else {
        displaySetLine(m, 6, COLOR_GREY, "Hold G0 5s: factory reset");
    }
}

[[noreturn]] void runSafeMode() {
    DisplayModel m = {};
    displaySetLine(m, 0, COLOR_YELLOW, "SAFE MODE");
    displaySetLine(m, 1, COLOR_WHITE, "USB = serial port");
    displaySetLine(m, 2, COLOR_WHITE, "(for flashing)");
    displaySetLine(m, 3, COLOR_GREY, "Network is off.");
    while (true) {
        const uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (ms >= SAFE_MODE_TIMEOUT_MS) {
            // Nobody flashed us: go back to work. Clearing the crash count
            // makes the next boot a normal one even after a crash loop.
            bootModeMarkStable();
            esp_restart();
        }
        displaySetLine(m, 4, COLOR_GREY, "Normal mode in %lus",
                       (unsigned long)((SAFE_MODE_TIMEOUT_MS - ms) / 1000));
        addResetHint(m);
        displayShow(m);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void buildStatus(DisplayModel& m, const Settings& s) {
    BridgeStatus st;
    bridgeStatus(st);
    const uint32_t cam = deviceIpForClients();
    char ip[16];
    displaySetLine(m, 0, COLOR_CYAN, "%s", s.ssid);
    displaySetLine(m, 1, COLOR_WHITE, "pass %s", s.password);
    displaySetLine(m, 2, COLOR_GREY, "cfg http://192.168.73.1");
    if (!st.usbMounted) {
        displaySetLine(m, 3, COLOR_YELLOW, "USB: no device");
        displaySetLine(m, 4, COLOR_GREY, "dev -");
    } else if (cam) {
        ipToStr(cam, ip);
        displaySetLine(m, 3, COLOR_GREEN, "USB: device OK");
        displaySetLine(m, 4, COLOR_GREEN, "dev %s", ip);
    } else if (st.deviceSeenIp) {
        ipToStr(st.deviceSeenIp, ip);
        displaySetLine(m, 3, COLOR_GREEN, "USB: device OK");
        displaySetLine(m, 4, COLOR_RED, "dev %s set DHCP!", ip);
    } else {
        displaySetLine(m, 3, COLOR_GREEN, "USB: device OK");
        displaySetLine(m, 4, COLOR_YELLOW, "dev waiting for IP");
    }
    displaySetLine(m, 5, st.wifiClients ? COLOR_GREEN : COLOR_GREY, "wifi clients %d",
                   st.wifiClients);
    addResetHint(m);
}
}  // namespace

extern "C" void app_main(void) {
    logBufferInit();
    const bool safe = bootModeIsSafe();
    initNvs();
    displayInit();
    buttonStart();
    if (safe) runSafeMode();

    Settings settings;
    settingsLoad(settings);
    ESP_LOGI(TAG, "ssid %s, channel %u, mDNS fallback '%s'", settings.ssid,
             (unsigned)settings.channel, settings.deviceHostname);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(bridgeStart(settings));
    dhcpTaskStart();
    mdnsTaskStart(settings.deviceHostname);
    webStart(settings);

    bool stable = false;
    bool otaConfirmed = false;
    DisplayModel m = {};
    while (true) {
        const int64_t ms = esp_timer_get_time() / 1000;
        if (!otaConfirmed && ms >= OTA_CONFIRM_MS && webRequestCount() > 0) {
            // A fresh image keeps its slot only once it has run 30 s and
            // actually served HTTP (tools/flash.sh polls /api/status). One
            // that crashes or never serves is rolled back on the next reset.
            if (webOtaPending()) ESP_LOGI(TAG, "new firmware confirmed");
            esp_ota_mark_app_valid_cancel_rollback();
            otaConfirmed = true;
        }
        if (!stable && ms >= BOOT_GUARD_STABLE_MS) {
            bootModeMarkStable();
            stable = true;
        }
        buildStatus(m, settings);
        displayShow(m);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
