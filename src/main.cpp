// CamCtrl Cardputer: EthAir (the Cardputer looks like a USB Ethernet adapter
// to the camera and bridges that link to its own Wi-Fi access point) plus a
// camera remote on the Cardputer's screen and keyboard.

#include <cstring>

#include "boot_mode.h"
#include "bridge.h"
#include "button.h"
#include "camera/camera_client.h"
#include "dhcp_task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw/keyboard.h"
#include "log_buffer.h"
#include "logic/boot_guard.h"
#include "logic/key_repeat.h"
#include "logic/remote_model.h"
#include "mdns_task.h"
#include "net_config.h"
#include "nvs_flash.h"
#include "settings_store.h"
#include "ui/ui.h"
#include "web.h"

namespace {
const char* TAG = "main";
constexpr int64_t OTA_CONFIRM_MS = 30000;
// A fresh OTA image that has not confirmed itself by then restarts, so the
// bootloader rolls back to the previous firmware without anyone touching the
// board (e.g. it runs but its network is broken).
constexpr int64_t OTA_GIVE_UP_MS = 90000;
// Free heap below this for LOW_HEAP_MS means the network can no longer
// allocate frames: restart rather than sit there unreachable.
constexpr size_t LOW_HEAP_BYTES = 12 * 1024;
constexpr int64_t LOW_HEAP_MS = 20000;
constexpr uint32_t TOAST_MS = 1500;
// No USB host after this long: likely plugged into a camera with the
// Cardputer's battery switch on (the camera then never starts its host).
constexpr uint32_t POWER_HINT_AFTER_MS = 15000;
constexpr uint32_t FRAME_MS = 20;
constexpr uint32_t REDRAW_MS = 250;

void initNvs() {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
}

uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

[[noreturn]] void runSafeMode() {
    while (true) {
        const uint32_t ms = nowMs();
        if (ms >= SAFE_MODE_TIMEOUT_MS) {
            // Nobody flashed us: go back to work. Clearing the crash count
            // makes the next boot a normal one even after a crash loop.
            bootModeMarkStable();
            esp_restart();
        }
        uiRenderSafeMode((SAFE_MODE_TIMEOUT_MS - ms) / 1000, buttonHeldMs());
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Keyboard + G0 -> remote model -> camera commands.
class Input {
public:
    explicit Input(QueueHandle_t keys) : keys_(keys) {}

    void poll(RemoteModel& model, char* toast, size_t toastCap, uint32_t& toastAt) {
        const uint32_t now = nowMs();
        KeyEvent e;
        while (keys_ && xQueueReceive(keys_, &e, 0) == pdTRUE) {
            if (!e.pressed) {
                if (repeater_.held() && e.row == repeatRow_ && e.col == repeatCol_) repeater_.release();
                continue;
            }
            const RemoteKey rk = remoteKeyFor(e.key);
            if (rk == RemoteKey::None) continue;
            handle(model, rk, keyboardShiftHeld(), toast, toastCap, toastAt);
            if (remoteKeyRepeats(rk)) {
                repeatKey_ = rk;
                repeatRow_ = e.row;
                repeatCol_ = e.col;
                repeater_.press(now);
            } else {
                repeater_.release();
            }
        }
        for (uint32_t n = repeater_.poll(now); n > 0; --n) {
            handle(model, repeatKey_, keyboardShiftHeld() || repeater_.boosted(), toast, toastCap, toastAt);
        }
        for (uint32_t n = buttonTakeShortPresses(); n > 0; --n) {
            handle(model, RemoteKey::AutoFocus, false, toast, toastCap, toastAt);
        }
    }

private:
    void handle(RemoteModel& model, RemoteKey key, bool big, char* toast, size_t cap, uint32_t& at) {
        const RemoteCommand c = cameraHandleKey(model, key, big);
        // Instant feedback for one-shot actions (the camera client confirms
        // or reports a failure a moment later).
        const char* text = nullptr;
        if (c.type == RemoteCmd::AutoFocus) text = "AUTOFOCUS";
        if (c.type == RemoteCmd::AutoWb) text = "AUTO WB";
        if (c.type == RemoteCmd::SetCleanFeed) text = c.b ? "CLEAN FEED ON" : "CLEAN FEED OFF";
        if (text) {
            snprintf(toast, cap, "%s", text);
            at = nowMs();
        }
    }

    QueueHandle_t keys_;
    KeyRepeater repeater_;
    RemoteKey repeatKey_ = RemoteKey::None;
    uint8_t repeatRow_ = 0xFF;
    uint8_t repeatCol_ = 0xFF;
};
}  // namespace

extern "C" void app_main(void) {
    logBufferInit();
    const bool safe = bootModeIsSafe();
    initNvs();
    uiInit();
    ESP_LOGI(TAG, "heap after ui: %lu free, %lu largest", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    buttonStart();
    if (safe) runSafeMode();

    Settings settings;
    settingsLoad(settings);
    ESP_LOGI(TAG, "ssid %s, channel %u, mDNS fallback '%s'", settings.ssid,
             (unsigned)settings.channel, settings.deviceHostname);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(bridgeStart(settings));
    ESP_LOGI(TAG, "heap after bridge: %lu free, %lu largest", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    dhcpTaskStart();
    mdnsTaskStart(settings.deviceHostname);
    webStart(settings);
    cameraClientStart();
    Input input(keyboardStart());
    ESP_LOGI(TAG, "heap after camera+keyboard: %lu free, %lu largest", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    RemoteModel model;
    UiState ui;
    UiState shown;
    bool everShown = false;
    snprintf(ui.ssid, sizeof(ui.ssid), "%s", settings.ssid);
    snprintf(ui.password, sizeof(ui.password), "%s", settings.password);
    char localToast[24] = "";
    uint32_t localToastAt = 0;
    uint32_t lastDraw = 0;
    uint32_t usbSeenAt = 0;
    bool stable = false;
    bool otaConfirmed = false;
    int64_t lowHeapSince = 0;
    uint32_t lastHeapLog = 0;

    while (true) {
        const uint32_t now = nowMs();
        if (!otaConfirmed && now >= OTA_CONFIRM_MS && webRequestCount() > 0) {
            // A fresh image keeps its slot only once it has run 30 s and
            // actually served HTTP (tools/flash.sh polls /api/status). One
            // that crashes or never serves is rolled back on the next reset.
            if (webOtaPending()) ESP_LOGI(TAG, "new firmware confirmed");
            esp_ota_mark_app_valid_cancel_rollback();
            otaConfirmed = true;
        }
        if (!otaConfirmed && (int64_t)now >= OTA_GIVE_UP_MS && webOtaPending()) {
            ESP_LOGE(TAG, "new firmware never confirmed itself; restarting to roll back");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        if (esp_get_free_heap_size() < LOW_HEAP_BYTES) {
            if (!lowHeapSince) lowHeapSince = now ? now : 1;
            if ((int64_t)now - lowHeapSince >= LOW_HEAP_MS) {
                ESP_LOGE(TAG, "free heap stayed below %u bytes; restarting", (unsigned)LOW_HEAP_BYTES);
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            lowHeapSince = 0;
        }
        if (!stable && now >= BOOT_GUARD_STABLE_MS) {
            bootModeMarkStable();
            stable = true;
        }

        if (now - lastHeapLog >= 30000) {
            lastHeapLog = now;
            ESP_LOGI(TAG, "heap: %lu free, %lu min, %lu largest", (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size(),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        input.poll(model, localToast, sizeof(localToast), localToastAt);

        cameraSnapshot(ui.cam, ui.phase, ui.liveEvents);
        model.clampFeedDisplay(ui.cam);
        ui.row = model.row();
        ui.feedDisplay = model.feedDisplay();
        ui.help = model.helpVisible();

        BridgeStatus bs;
        bridgeStatus(bs);
        ui.wifiClients = bs.wifiClients;
        if (bs.usbMounted) usbSeenAt = now;
        ui.powerSwitchHint = ui.phase == CameraPhase::NoUsb && now - usbSeenAt >= POWER_HINT_AFTER_MS &&
                             now >= POWER_HINT_AFTER_MS;

        // Newest toast wins: a local action or a notice from the camera client.
        char notice[24];
        uint32_t noticeAt = 0;
        cameraNotice(notice, sizeof(notice), noticeAt);
        ui.toast[0] = '\0';
        if (noticeAt && now - noticeAt < TOAST_MS && noticeAt >= localToastAt) {
            snprintf(ui.toast, sizeof(ui.toast), "%s", notice);
        } else if (localToastAt && now - localToastAt < TOAST_MS) {
            snprintf(ui.toast, sizeof(ui.toast), "%s", localToast);
        }
        ui.resetHeldMs = buttonHeldMs();

        // Redraw on any change, and now and then regardless.
        if (!everShown || memcmp(&ui, &shown, sizeof(ui)) != 0 || now - lastDraw >= REDRAW_MS) {
            uiRender(ui);
            shown = ui;
            everShown = true;
            lastDraw = now;
        }
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}
