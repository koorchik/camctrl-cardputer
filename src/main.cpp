// CamCtrl Cardputer: EthAir (the Cardputer looks like a USB Ethernet adapter
// to the camera and bridges that link to its own Wi-Fi access point) plus a
// camera remote on the Cardputer's screen and keyboard.

#include <cstdlib>
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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "hw/keyboard.h"
#include "log_buffer.h"
#include "logic/boot_guard.h"
#include "logic/key_repeat.h"
#include "logic/remote_model.h"
#include "logic/ui_nav.h"
#include "mdns_task.h"
#include "net_config.h"
#include "nvs_flash.h"
#include "settings_store.h"
#include "ui/ui.h"
#include "web.h"

namespace {
// Screenshots (/api/screen): the web task asks, the main loop draws a frame
// band by band and waits while the web task sends each band.
SemaphoreHandle_t s_capRequest = nullptr;
SemaphoreHandle_t s_capBand = nullptr;
SemaphoreHandle_t s_capSent = nullptr;
SemaphoreHandle_t s_capOne = nullptr;  // one capture at a time
const uint8_t* s_capPixels = nullptr;
int s_capRows = 0;
QueueHandle_t s_keyQueue = nullptr;  // for /api/key

void captureSink(const uint8_t* pixels, int, int rows, void*) {
    s_capPixels = pixels;
    s_capRows = rows;
    xSemaphoreGive(s_capBand);
    xSemaphoreTake(s_capSent, pdMS_TO_TICKS(2000));  // the web task sent it (or gave up)
}

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
// No USB host after this long: likely plugged into a camera with the
// Cardputer's battery switch on (the camera then never starts its host).
constexpr uint32_t POWER_HINT_AFTER_MS = 15000;
constexpr uint32_t FRAME_MS = 33;
constexpr uint32_t REDRAW_MS = 500;

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
        uiRenderSafeMode((SAFE_MODE_TIMEOUT_MS - ms) / 1000, SAFE_MODE_TIMEOUT_MS / 1000, buttonHeldMs());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int isoIndex(const CameraValues& v) {
    if (!v.isoCount) return -1;
    size_t best = 0;
    for (size_t i = 1; i < v.isoCount; ++i) {
        if (abs(v.isoList[i] - v.iso) < abs(v.isoList[best] - v.iso)) best = i;
    }
    return (int)best;
}

bool isModifier(const Key& k) {
    return k.code == KeyCode::Shift || k.code == KeyCode::Fn || k.code == KeyCode::Ctrl ||
           k.code == KeyCode::Opt || k.code == KeyCode::Alt;
}

// Keyboard + G0 -> screens (UiNav) -> remote model -> camera commands.
class Remote {
public:
    Remote(QueueHandle_t keys, bool hintsSlim) : keys_(keys), hints_(hintsSlim) {}

    UiNav& nav() { return nav_; }
    RemoteModel& model() { return model_; }
    HintTracker& hints() { return hints_; }
    UiState& ui() { return ui_; }

    void poll(uint32_t now, bool connected) {
        KeyEvent e;
        while (keys_ && xQueueReceive(keys_, &e, 0) == pdTRUE) {
            if (!e.pressed) {
                if (repeater_.held() && e.row == repeatRow_ && e.col == repeatCol_) repeater_.release();
                continue;
            }
            const RemoteKey rk = remoteKeyFor(e.key);
            if (rk == RemoteKey::None) {
                if (!isModifier(e.key) && hints_.onUnmappedKey(now)) {
                    cameraLocalNotice("PRESS h FOR KEYS", NoticeKind::Info);
                }
                continue;
            }
            const UiView viewBefore = nav_.view();
            key(rk, keyboardShiftHeld(), now, connected);
            // Only adjusting keys repeat, and only on a control view (a key
            // that closed help or the intro must not start adjusting).
            if (remoteKeyRepeats(rk) && viewBefore != UiView::Help && viewBefore != UiView::Intro &&
                nav_.view() != UiView::Help && nav_.view() != UiView::Intro) {
                repeatKey_ = rk;
                repeatRow_ = e.row;
                repeatCol_ = e.col;
                repeater_.press(now);
            } else {
                repeater_.release();
            }
        }
        for (uint32_t n = repeater_.poll(now); n > 0; --n) {
            key(repeatKey_, keyboardShiftHeld() || repeater_.boosted(), now, connected);
        }
        // Up/Down held in a detail view are big steps too (the hint lights up).
        const bool upDownHeld = repeater_.held() && nav_.view() == UiView::Detail &&
                                (repeatKey_ == RemoteKey::Up || repeatKey_ == RemoteKey::Down);
        ui_.bigStep = keyboardShiftHeld() || (repeater_.held() && repeater_.boosted()) || upDownHeld;
        for (uint32_t n = buttonTakeShortPresses(); n > 0; --n) key(RemoteKey::AutoFocus, false, now, connected);
    }

    bool keyHeld() const { return repeater_.held(); }

private:
    void key(RemoteKey k, bool big, uint32_t now, bool connected) {
        ui_.pressedKey = k;
        ui_.pressedAt = now;
        const UiView before = nav_.view();
        const RemoteRow rowBefore = model_.row();
        const NavKeys nk = nav_.onKey(k, now, connected);
        if (before == UiView::Help && nav_.view() == UiView::Intro) {
            hints_.reset();  // replaying the intro brings the full hints back
            settingsSetUiFlag("hslim", false);
        }
        for (int i = 0; i < nk.count; ++i) {
            const RemoteKey mk = nk.keys[i];
            CameraView cv;
            cameraSnapshot(cv);
            const RemoteCommand c = cameraHandleKey(model_, mk, big || nk.big);
            switch (c.type) {
                case RemoteCmd::AutoFocus:
                    cameraLocalNotice("AUTOFOCUS", NoticeKind::Info);
                    hints_.onAutofocus();
                    break;
                case RemoteCmd::AutoWb:
                    cameraLocalNotice("AUTO WHITE BALANCE", NoticeKind::Info);
                    break;
                case RemoteCmd::None:
                    if (mk == RemoteKey::Left || mk == RemoteKey::Right) {
                        if (model_.limitHit()) {
                            ui_.limitHitAt = now;
                        } else if (model_.row() == RemoteRow::Focus && !cv.shown.focusControllable) {
                            ui_.unavailableAt = now;
                            cameraLocalNotice("LENS NOT CONTROLLABLE", NoticeKind::Warn);
                        } else if (model_.row() == RemoteRow::Iris && cv.shown.irisAuto) {
                            ui_.unavailableAt = now;
                            cameraLocalNotice("IRIS ON AUTO EXPOSURE", NoticeKind::Warn);
                        } else if (model_.row() == RemoteRow::Iris && cv.shown.irisKnown &&
                                   (!cv.shown.irisControllable || cv.shown.irisStop <= 0)) {
                            ui_.unavailableAt = now;
                            cameraLocalNotice("LENS NOT CONTROLLABLE", NoticeKind::Warn);
                        }
                    }
                    break;
                default:
                    hints_.onConfirmedAdjust();
                    break;
            }
            if (mk == RemoteKey::Up || mk == RemoteKey::Down || mk == RemoteKey::ColLeft ||
                mk == RemoteKey::ColRight || (mk >= RemoteKey::JumpFocus && mk <= RemoteKey::JumpTint)) {
                hints_.onNavigate();
            }
        }
        // The EV chip counts from the ISO the detail view opened with.
        if (nav_.view() == UiView::Detail && (before != UiView::Detail || model_.row() != rowBefore)) {
            CameraView cv;
            cameraSnapshot(cv);
            ui_.isoIndexAtOpen = isoIndex(cv.shown);
        }
        if (hints_.takeGraduated()) settingsSetUiFlag("hslim", true);
        if (nav_.introFinished() && !introSaved_) {
            introSaved_ = true;
            settingsSetUiFlag("intro", true);
        }
    }

    QueueHandle_t keys_;
    RemoteModel model_;
    UiNav nav_;
    HintTracker hints_;
    UiState ui_;
    KeyRepeater repeater_;
    RemoteKey repeatKey_ = RemoteKey::None;
    uint8_t repeatRow_ = 0xFF;
    uint8_t repeatCol_ = 0xFF;
    bool introSaved_ = false;
};
}  // namespace

extern "C" esp_err_t webScreenCapture(httpd_req_t* req) {
    if (!s_capRequest) return httpd_resp_send_404(req);
    if (xSemaphoreTake(s_capOne, pdMS_TO_TICKS(3000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
        return ESP_FAIL;
    }
    xSemaphoreTake(s_capBand, 0);  // drop anything left from an aborted capture
    xSemaphoreTake(s_capSent, 0);
    xSemaphoreGive(s_capRequest);
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = ESP_OK;
    int rows = 0;
    while (rows < 135) {
        if (xSemaphoreTake(s_capBand, pdMS_TO_TICKS(3000)) != pdTRUE) {
            err = ESP_FAIL;
            break;
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(s_capPixels), s_capRows * 240 * 2);
        }
        rows += s_capRows;
        xSemaphoreGive(s_capSent);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, nullptr, 0);
    xSemaphoreGive(s_capOne);
    return err;
}

// Key names for /api/key: up down left right enter space back h f i s o w t c.
extern "C" bool webInjectKey(const char* name) {
    if (!s_keyQueue || !name) return false;
    static const struct {
        const char* name;
        Key key;
    } kKeys[] = {
        {"up", {KeyCode::Char, ';'}},    {"down", {KeyCode::Char, '.'}},  {"left", {KeyCode::Char, ','}},
        {"right", {KeyCode::Char, '/'}}, {"enter", {KeyCode::Enter, 0}},  {"space", {KeyCode::Char, ' '}},
        {"back", {KeyCode::Backspace, 0}}, {"h", {KeyCode::Char, 'h'}},  {"f", {KeyCode::Char, 'f'}},
        {"i", {KeyCode::Char, 'i'}},     {"s", {KeyCode::Char, 's'}},     {"o", {KeyCode::Char, 'o'}},     {"w", {KeyCode::Char, 'w'}},
        {"t", {KeyCode::Char, 't'}},     {"c", {KeyCode::Char, 'c'}},
    };
    for (const auto& k : kKeys) {
        if (strcmp(k.name, name) != 0) continue;
        // Row/col outside the matrix: never matches a physical key's release.
        KeyEvent press{k.key, true, 0xFE, 0xFE};
        KeyEvent release{k.key, false, 0xFE, 0xFE};
        return xQueueSend(s_keyQueue, &press, 0) == pdTRUE && xQueueSend(s_keyQueue, &release, 0) == pdTRUE;
    }
    return false;
}

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
    QueueHandle_t keyboard = keyboardStart();
    s_keyQueue = keyboard;
    s_capBand = xSemaphoreCreateBinary();
    s_capSent = xSemaphoreCreateBinary();
    s_capOne = xSemaphoreCreateMutex();
    s_capRequest = xSemaphoreCreateBinary();  // last: webScreenCapture checks it
    ESP_LOGI(TAG, "heap after camera+keyboard: %lu free, %lu largest", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Large objects live outside the main task's stack.
    static Remote remote(keyboard, settingsUiFlag("hslim"));
    bool introSeen = settingsUiFlag("intro");
    UiState& ui = remote.ui();
    static UiState shown;
    bool everShown = false;
    snprintf(ui.ssid, sizeof(ui.ssid), "%s", settings.ssid);
    snprintf(ui.password, sizeof(ui.password), "%s", settings.password);
    uint32_t lastDraw = 0;
    uint32_t usbSeenAt = 0;
    bool animating = false;
    OpState afPrev = OpState::Idle;
    uint32_t resetHeldPrev = 0;
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
        // Total free heap can look fine while it is too fragmented to
        // allocate a frame, so the largest block counts too.
        if (esp_get_free_heap_size() < LOW_HEAP_BYTES ||
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 4096) {
            // 64-bit milliseconds: the 32-bit clock wraps after 49 days.
            const int64_t nowMs64 = esp_timer_get_time() / 1000;
            if (!lowHeapSince) lowHeapSince = nowMs64 ? nowMs64 : 1;
            if (nowMs64 - lowHeapSince >= LOW_HEAP_MS) {
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

        cameraSnapshot(ui.cam);
        const bool connected = ui.cam.phase == CameraPhase::Connected;
        if (connected && !introSeen) {  // first connection ever: the intro
            introSeen = true;
            remote.nav().startIntro();
        }
        remote.poll(now, connected);
        cameraSnapshot(ui.cam);
        if (afPrev == OpState::Running && (ui.cam.af == OpState::Done || ui.cam.af == OpState::Timeout)) {
            remote.nav().onAutofocusDone(now);
        }
        afPrev = ui.cam.af;
        const bool busy = remote.keyHeld() || ui.cam.af == OpState::Running || ui.cam.awb == OpState::Running ||
                          ui.cam.irisQueued != 0;
        remote.nav().tick(now, busy);
        remote.model().clampFeedDisplay(ui.cam.shown);
        ui.view = remote.nav().view();
        ui.introCard = remote.nav().introCard();
        ui.helpPage = remote.nav().helpPage();
        ui.row = remote.model().row();
        ui.feedDisplay = remote.model().feedDisplay();
        ui.hintsSlim = remote.hints().slim();

        BridgeStatus bs;
        bridgeStatus(bs);
        ui.wifiClients = bs.wifiClients;
        if (bs.usbMounted) usbSeenAt = now;
        ui.powerSwitchHint = ui.cam.phase == CameraPhase::NoUsb && now - usbSeenAt >= POWER_HINT_AFTER_MS &&
                             now >= POWER_HINT_AFTER_MS;
        ui.resetHeldMs = buttonHeldMs();
        if (resetHeldPrev >= 1000 && ui.resetHeldMs == 0) cameraLocalNotice("RESET CANCELLED", NoticeKind::Warn);
        resetHeldPrev = ui.resetHeldMs;

        // Draw on change, at ~30 fps while animating, and twice a second anyway.
        ui.nowMs = shown.nowMs;
        const bool changed = !everShown || memcmp(&ui, &shown, sizeof(ui)) != 0;
        ui.nowMs = now;
        if (changed || (animating && now - lastDraw >= FRAME_MS) || now - lastDraw >= REDRAW_MS) {
            animating = uiRender(ui);
            shown = ui;
            everShown = true;
            lastDraw = now;
        }
        if (xSemaphoreTake(s_capRequest, 0) == pdTRUE) uiCapture(ui, captureSink, nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
