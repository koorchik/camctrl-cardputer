#include "settings_store.h"

#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"

namespace {
const char* TAG = "settings";
const char* NS = "ethair";

void readStr(nvs_handle_t h, const char* key, char* out, size_t cap) {
    char tmp[80];
    size_t len = sizeof(tmp);
    if (nvs_get_str(h, key, tmp, &len) == ESP_OK && len <= cap) {
        memcpy(out, tmp, len);
    }
}
}  // namespace

void settingsDeviceMac(uint8_t mac[6]) {
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
}

void settingsLoad(Settings& out) {
    uint8_t mac[6];
    settingsDeviceMac(mac);
    Settings defaults;
    defaultSettings(mac, defaults);
    out = defaults;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        return;
    }
    readStr(h, "ssid", out.ssid, sizeof(out.ssid));
    readStr(h, "pass", out.password, sizeof(out.password));
    readStr(h, "host", out.deviceHostname, sizeof(out.deviceHostname));
    uint8_t ch = 0;
    if (nvs_get_u8(h, "chan", &ch) == ESP_OK) out.channel = ch;
    nvs_close(h);

    if (const char* err = validateSettings(out)) {
        ESP_LOGW(TAG, "saved settings invalid (%s), using defaults", err);
        out = defaults;
    }
}

const char* settingsSave(const Settings& s) {
    if (const char* err = validateSettings(s)) return err;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return "Could not open settings storage";
    esp_err_t e = nvs_set_str(h, "ssid", s.ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "pass", s.password);
    if (e == ESP_OK) e = nvs_set_str(h, "host", s.deviceHostname);
    if (e == ESP_OK) e = nvs_set_u8(h, "chan", s.channel);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? nullptr : "Could not save settings";
}

bool settingsUiFlag(const char* key) {
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v != 0;
}

void settingsSetUiFlag(const char* key, bool value) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

void settingsErase() {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "settings erased");
}
