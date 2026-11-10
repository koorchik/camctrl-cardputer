#include "camera/camera_client.h"

#include <cstdio>
#include <cstring>

#include "bridge.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "logic/camera_json.h"
#include "mdns_task.h"
#include "net_config.h"

namespace {
const char* TAG = "camera";

constexpr uint32_t HTTP_TIMEOUT_MS = 2500;
constexpr uint32_t FOCUS_INTERVAL_MS = 50;      // throttle while a key is held
constexpr uint32_t IRIS_SETTLE_MS = 300;        // one iris write in flight
constexpr uint32_t LOCAL_HOLD_MS = 800;         // ignore reports this long after a local change
constexpr uint32_t POLL_MS = 2000;              // REST polling while the WebSocket is down
constexpr uint32_t DESCRIPTION_POLL_MS = 10000; // iris range changes with zoom
constexpr int MAX_FAILURES = 3;

SemaphoreHandle_t s_lock = nullptr;
CameraValues s_values;
CameraPhase s_phase = CameraPhase::NoUsb;
QueueHandle_t s_cmdQueue = nullptr;
esp_websocket_client_handle_t s_ws = nullptr;
volatile bool s_wsLive = false;  // subscribed and receiving events

char s_notice[24] = "";
uint32_t s_noticeMs = 0;

// When the user last changed each value here (ms), so stale camera reports
// do not snap the screen back while the camera catches up.
struct Holds {
    uint32_t focus = 0, iso = 0, wb = 0, tint = 0;
    uint32_t feed[CAM_MAX_DISPLAYS] = {};
} s_holds;

uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool held(uint32_t at, uint32_t now) {
    return at && now - at < LOCAL_HOLD_MS;
}

void notice(const char* text) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_notice, sizeof(s_notice), "%s", text);
    s_noticeMs = nowMs();
    xSemaphoreGive(s_lock);
}

// Applies a camera report to the shared values, keeping recently changed
// local values. Caller holds s_lock.
template <typename Fn>
void applyKeepingLocal(Fn apply) {
    const uint32_t now = nowMs();
    const CameraValues before = s_values;
    apply(s_values);
    if (held(s_holds.focus, now)) s_values.focus = before.focus;
    if (held(s_holds.iso, now)) s_values.iso = before.iso;
    if (held(s_holds.wb, now)) s_values.wb = before.wb;
    if (held(s_holds.tint, now)) s_values.tint = before.tint;
    for (size_t i = 0; i < CAM_MAX_DISPLAYS; ++i) {
        if (held(s_holds.feed[i], now) && i < s_values.displayCount &&
            strcmp(before.displays[i], s_values.displays[i]) == 0) {
            s_values.cleanFeed[i] = before.cleanFeed[i];
        }
    }
}

// --- HTTP -------------------------------------------------------------------
struct Body {
    char buf[1536];
    size_t len;
};

esp_err_t httpEvent(esp_http_client_event_t* e) {
    if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
        Body* b = (Body*)e->user_data;
        const size_t room = sizeof(b->buf) - 1 - b->len;
        const size_t n = (size_t)e->data_len < room ? (size_t)e->data_len : room;
        memcpy(b->buf + b->len, e->data, n);
        b->len += n;
        b->buf[b->len] = '\0';
    }
    return ESP_OK;
}

class Http {
public:
    void open(uint32_t ip) {
        close();
        char url[64];
        char ipStr[16];
        ipToStr(ip, ipStr);
        snprintf(url, sizeof(url), "http://%s/control/api/v1/system/product", ipStr);
        snprintf(base_, sizeof(base_), "http://%s/control/api/v1", ipStr);
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = HTTP_TIMEOUT_MS;
        cfg.event_handler = httpEvent;
        cfg.user_data = &body_;
        cfg.keep_alive_enable = true;
        cfg.disable_auto_redirect = true;
        client_ = esp_http_client_init(&cfg);
    }
    void close() {
        if (client_) esp_http_client_cleanup(client_);
        client_ = nullptr;
    }
    // Returns the HTTP status, or -1 on a transport error. The body is in body().
    int request(esp_http_client_method_t method, const char* path, const char* json = nullptr) {
        if (!client_) return -1;
        char url[160];
        snprintf(url, sizeof(url), "%s%s", base_, path);
        body_.len = 0;
        body_.buf[0] = '\0';
        esp_http_client_set_url(client_, url);
        esp_http_client_set_method(client_, method);
        if (json) {
            esp_http_client_set_header(client_, "Content-Type", "application/json");
            esp_http_client_set_post_field(client_, json, (int)strlen(json));
        } else {
            esp_http_client_delete_header(client_, "Content-Type");
            esp_http_client_set_post_field(client_, nullptr, 0);
        }
        if (esp_http_client_perform(client_) != ESP_OK) return -1;
        return esp_http_client_get_status_code(client_);
    }
    const Body& body() const { return body_; }

private:
    esp_http_client_handle_t client_ = nullptr;
    char base_[48] = "";
    Body body_ = {};
};

Http s_http;
int s_failures = 0;

// GET path and apply it. Returns false on a transport/HTTP error.
bool getAndApply(const char* path) {
    const int status = s_http.request(HTTP_METHOD_GET, path);
    if (status < 0) {
        ++s_failures;
        return false;
    }
    s_failures = 0;
    if (status != 200) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    applyKeepingLocal([&](CameraValues& v) {
        cameraApplyJson(v, path, s_http.body().buf, s_http.body().len);
    });
    xSemaphoreGive(s_lock);
    return true;
}

bool put(const char* path, const char* json) {
    const int status = s_http.request(HTTP_METHOD_PUT, path, json);
    if (status < 0) {
        ++s_failures;
    } else {
        s_failures = 0;
    }
    const bool ok = status == 200 || status == 204;
    if (!ok) {
        ESP_LOGW(TAG, "PUT %s -> %d %s", path, status, s_http.body().buf);
        notice(status == 403 ? "NOT ALLOWED NOW" : "WRITE FAILED");
    }
    return ok;
}

void cleanFeedPath(const CameraValues& v, int display, char* out, size_t cap) {
    char enc[3 * CAM_DISPLAY_NAME_MAX + 1];
    cameraEncodePathSegment(v.displays[display], enc, sizeof(enc));
    snprintf(out, cap, "/monitoring/%s/cleanFeed", enc);
}

void fetchCleanFeeds() {
    CameraValues v;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    v = s_values;
    xSemaphoreGive(s_lock);
    for (size_t i = 0; i < v.displayCount; ++i) {
        char path[96];
        cleanFeedPath(v, (int)i, path, sizeof(path));
        getAndApply(path);
    }
}

void pollValues() {
    getAndApply("/lens/focus");
    getAndApply("/lens/iris");
    getAndApply("/video/iso");
    getAndApply("/video/whiteBalance");
    getAndApply("/video/whiteBalanceTint");
    fetchCleanFeeds();
}

bool fetchAll() {
    if (!getAndApply("/system/product")) return false;
    const char* paths[] = {
        "/lens/focus/description", "/lens/iris/description", "/video/supportedISOs",
        "/video/whiteBalance/description", "/video/whiteBalanceTint/description",
        "/monitoring/display",
    };
    for (const char* p : paths) getAndApply(p);
    pollValues();
    return s_failures == 0;
}

// --- WebSocket --------------------------------------------------------------
char s_wsBuf[4096];
size_t s_wsLen = 0;

void wsEvent(void*, esp_event_base_t, int32_t id, void* data) {
    auto* d = (esp_websocket_event_data_t*)data;
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "websocket connected");
            s_wsLen = 0;
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            s_wsLive = false;
            break;
        case WEBSOCKET_EVENT_DATA: {
            if (d->op_code != 0x1 && d->op_code != 0x0) break;  // text / continuation
            if (d->payload_offset == 0) s_wsLen = 0;
            if (s_wsLen + d->data_len >= sizeof(s_wsBuf)) {
                s_wsLen = sizeof(s_wsBuf);  // too big: drop this message
                break;
            }
            memcpy(s_wsBuf + s_wsLen, d->data_ptr, d->data_len);
            s_wsLen += d->data_len;
            if (d->payload_offset + d->data_len < d->payload_len) break;  // more fragments
            if (s_wsLen >= sizeof(s_wsBuf)) break;
            CameraWsKind kind = CameraWsKind::Other;
            CameraValues snapshot;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            applyKeepingLocal([&](CameraValues& v) { kind = cameraApplyWsMessage(v, s_wsBuf, s_wsLen); });
            snapshot = s_values;
            xSemaphoreGive(s_lock);
            if (kind == CameraWsKind::Opened) {
                char sub[768];
                const size_t n = cameraSubscribeMessage(snapshot, sub, sizeof(sub));
                if (n && esp_websocket_client_send_text(s_ws, sub, (int)n, pdMS_TO_TICKS(1000)) > 0) {
                    s_wsLive = true;
                    ESP_LOGI(TAG, "subscribed to camera events");
                }
            }
            break;
        }
        default:
            break;
    }
}

void wsStart(uint32_t ip) {
    char ipStr[16];
    ipToStr(ip, ipStr);
    char uri[80];
    snprintf(uri, sizeof(uri), "ws://%s/control/api/v1/event/websocket", ipStr);
    esp_websocket_client_config_t cfg = {};
    cfg.uri = uri;
    cfg.reconnect_timeout_ms = 3000;
    cfg.network_timeout_ms = 5000;
    cfg.buffer_size = 1024;
    cfg.task_stack = 5120;
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) return;
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, wsEvent, nullptr);
    esp_websocket_client_start(s_ws);
}

void wsStop() {
    s_wsLive = false;
    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
    }
}

// --- commands ---------------------------------------------------------------
struct Pending {
    bool focus = false;
    float focusValue = 0;
    int irisSteps = 0;
    bool autoFocus = false;
    bool iso = false;
    int isoValue = 0;
    bool wb = false;
    int wbValue = 0;
    bool autoWb = false;
    bool tint = false;
    int tintValue = 0;
    bool feed[CAM_MAX_DISPLAYS] = {};
    bool feedValue[CAM_MAX_DISPLAYS] = {};
};

void collect(Pending& p, const RemoteCommand& c) {
    switch (c.type) {
        case RemoteCmd::SetFocus: p.focus = true; p.focusValue = c.f; break;
        case RemoteCmd::AutoFocus: p.autoFocus = true; p.focus = false; break;
        case RemoteCmd::IrisStep: p.irisSteps += c.i; break;
        case RemoteCmd::SetIso: p.iso = true; p.isoValue = c.i; break;
        case RemoteCmd::SetWb: p.wb = true; p.wbValue = c.i; p.autoWb = false; break;
        case RemoteCmd::AutoWb: p.autoWb = true; p.wb = false; break;
        case RemoteCmd::SetTint: p.tint = true; p.tintValue = c.i; break;
        case RemoteCmd::SetCleanFeed:
            if (c.display >= 0 && c.display < (int)CAM_MAX_DISPLAYS) {
                p.feed[c.display] = true;
                p.feedValue[c.display] = c.b;
            }
            break;
        default: break;
    }
}

struct Timers {
    uint32_t lastFocus = 0;
    uint32_t irisBusyUntil = 0;
    uint32_t refocusAt = 0;   // re-read focus after autofocus
    uint32_t rewbAt = 0;      // re-read WB after auto WB
    uint32_t reirisAt = 0;    // re-read iris when events are not live
};

void sendPending(Pending& p, Timers& t) {
    const uint32_t now = nowMs();
    char json[96];
    if (p.autoFocus) {
        p.autoFocus = false;
        notice("AUTOFOCUS");
        put("/lens/focus/doAutoFocus", "{\"position\":{\"x\":0.5,\"y\":0.5}}");
        t.refocusAt = now + 700;
    }
    if (p.focus && now - t.lastFocus >= FOCUS_INTERVAL_MS) {
        p.focus = false;
        t.lastFocus = now;
        snprintf(json, sizeof(json), "{\"normalised\":%.4f}", p.focusValue);
        put("/lens/focus", json);
    }
    if (p.irisSteps && (int32_t)(now - t.irisBusyUntil) >= 0) {
        int steps = p.irisSteps;
        steps = steps > 127 ? 127 : (steps < -128 ? -128 : steps);
        p.irisSteps -= steps;
        snprintf(json, sizeof(json), "{\"adjustmentStep\":%d}", steps);
        put("/lens/iris", json);
        t.irisBusyUntil = now + IRIS_SETTLE_MS;
        t.reirisAt = now + IRIS_SETTLE_MS;
    }
    if (p.iso) {
        p.iso = false;
        snprintf(json, sizeof(json), "{\"iso\":%d}", p.isoValue);
        put("/video/iso", json);
    }
    if (p.autoWb) {
        p.autoWb = false;
        notice("AUTO WB");
        put("/video/whiteBalance/doAuto", "{}");
        t.rewbAt = now + 500;
    }
    if (p.wb) {
        p.wb = false;
        snprintf(json, sizeof(json), "{\"whiteBalance\":%d}", p.wbValue);
        put("/video/whiteBalance", json);
    }
    if (p.tint) {
        p.tint = false;
        snprintf(json, sizeof(json), "{\"whiteBalanceTint\":%d}", p.tintValue);
        put("/video/whiteBalanceTint", json);
    }
    for (size_t i = 0; i < CAM_MAX_DISPLAYS; ++i) {
        if (!p.feed[i]) continue;
        p.feed[i] = false;
        CameraValues v;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        v = s_values;
        xSemaphoreGive(s_lock);
        if (i >= v.displayCount) continue;
        char path[96];
        cleanFeedPath(v, (int)i, path, sizeof(path));
        snprintf(json, sizeof(json), "{\"enabled\":%s}", p.feedValue[i] ? "true" : "false");
        if (!put(path, json)) getAndApply(path);  // show what the camera really has
    }
    // Read-backs (WebSocket events usually make these redundant).
    if (t.refocusAt && (int32_t)(now - t.refocusAt) >= 0) {
        t.refocusAt = 0;
        getAndApply("/lens/focus");
    }
    if (t.rewbAt && (int32_t)(now - t.rewbAt) >= 0) {
        t.rewbAt = 0;
        getAndApply("/video/whiteBalance");
        getAndApply("/video/whiteBalanceTint");
    }
    if (t.reirisAt && (int32_t)(now - t.reirisAt) >= 0 && !s_wsLive) {
        t.reirisAt = 0;
        getAndApply("/lens/iris");
    }
}

bool pendingWork(const Pending& p) {
    if (p.focus || p.irisSteps || p.autoFocus || p.iso || p.wb || p.autoWb || p.tint) return true;
    for (bool f : p.feed) {
        if (f) return true;
    }
    return false;
}

void setPhase(CameraPhase phase) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_phase != phase) ESP_LOGI(TAG, "phase %d -> %d", (int)s_phase, (int)phase);
    s_phase = phase;
    if (phase != CameraPhase::Connected) s_values.connected = false;
    xSemaphoreGive(s_lock);
}

void task(void*) {
    Pending pending;
    Timers timers;
    uint32_t connectedIp = 0;
    uint32_t lastPoll = 0;
    uint32_t lastDescription = 0;
    uint32_t nextAttempt = 0;

    while (true) {
        // Wait for commands, or tick every 20 ms.
        RemoteCommand c;
        if (xQueueReceive(s_cmdQueue, &c, pdMS_TO_TICKS(20)) == pdTRUE) {
            do {
                collect(pending, c);
            } while (xQueueReceive(s_cmdQueue, &c, 0) == pdTRUE);
        }
        const uint32_t now = nowMs();

        BridgeStatus bs;
        bridgeStatus(bs);
        const uint32_t ip = deviceIpForClients();
        if (!bs.usbMounted || !ip || (connectedIp && ip != connectedIp)) {
            if (connectedIp) {
                ESP_LOGI(TAG, "camera link lost");
                wsStop();
                s_http.close();
                connectedIp = 0;
                pending = Pending();
            }
            setPhase(!bs.usbMounted ? CameraPhase::NoUsb : CameraPhase::WaitingIp);
            continue;
        }

        if (!connectedIp) {
            if ((int32_t)(now - nextAttempt) < 0) continue;
            setPhase(CameraPhase::Connecting);
            s_http.open(ip);
            s_failures = 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_values = CameraValues();
            xSemaphoreGive(s_lock);
            if (!fetchAll()) {
                s_http.close();
                nextAttempt = now + 2000;
                continue;
            }
            connectedIp = ip;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_values.connected = true;
            s_phase = CameraPhase::Connected;
            ESP_LOGI(TAG, "connected to %s", s_values.name);
            xSemaphoreGive(s_lock);
            wsStart(ip);
            lastPoll = lastDescription = now;
            pending = Pending();
            continue;
        }

        if (pendingWork(pending) || timers.refocusAt || timers.rewbAt || timers.reirisAt) {
            sendPending(pending, timers);
        }
        if (!s_wsLive && now - lastPoll >= POLL_MS) {
            lastPoll = now;
            pollValues();
        }
        if (now - lastDescription >= DESCRIPTION_POLL_MS) {
            lastDescription = now;
            getAndApply("/lens/iris/description");
            if (!s_wsLive) getAndApply("/lens/focus/description");
        }
        if (s_failures >= MAX_FAILURES) {
            ESP_LOGW(TAG, "camera stopped answering; reconnecting");
            wsStop();
            s_http.close();
            connectedIp = 0;
            nextAttempt = now + 1000;
            setPhase(CameraPhase::Connecting);
        }
    }
}
}  // namespace

void cameraClientStart() {
    s_lock = xSemaphoreCreateMutex();
    s_cmdQueue = xQueueCreate(32, sizeof(RemoteCommand));
    xTaskCreate(task, "camera", 6144, nullptr, 5, nullptr);
}

void cameraSnapshot(CameraValues& out, CameraPhase& phase, bool& liveEvents) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out = s_values;
    phase = s_phase;
    xSemaphoreGive(s_lock);
    liveEvents = s_wsLive;
}

RemoteCommand cameraHandleKey(RemoteModel& model, RemoteKey key, bool big) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const RemoteCommand c = model.onKey(key, big, s_values);
    const uint32_t now = nowMs();
    switch (c.type) {
        case RemoteCmd::SetFocus: s_holds.focus = now; break;
        case RemoteCmd::SetIso: s_holds.iso = now; break;
        case RemoteCmd::SetWb: s_holds.wb = now; break;
        case RemoteCmd::SetTint: s_holds.tint = now; break;
        case RemoteCmd::SetCleanFeed:
            if (c.display >= 0 && c.display < (int)CAM_MAX_DISPLAYS) s_holds.feed[c.display] = now;
            break;
        default: break;
    }
    const bool connected = s_phase == CameraPhase::Connected;
    xSemaphoreGive(s_lock);
    if (c.type != RemoteCmd::None && connected) xQueueSend(s_cmdQueue, &c, 0);
    return c;
}

void cameraNotice(char* out, size_t cap, uint32_t& atMs) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out, cap, "%s", s_notice);
    atMs = s_noticeMs;
    xSemaphoreGive(s_lock);
}
