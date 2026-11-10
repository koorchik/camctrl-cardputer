#include "logic/settings.h"

#include <cstdio>
#include <cstring>

namespace {
bool printable(const char* s) {
    for (; *s; ++s) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false;
    }
    return true;
}
}  // namespace

void defaultSettings(const uint8_t mac[6], Settings& out) {
    memset(&out, 0, sizeof(out));
    snprintf(out.ssid, sizeof(out.ssid), "%s-%02X%02X", DEFAULT_SSID_PREFIX, mac[4], mac[5]);
    snprintf(out.password, sizeof(out.password), "%s", DEFAULT_PASSWORD);
    out.channel = DEFAULT_CHANNEL;
    snprintf(out.deviceHostname, sizeof(out.deviceHostname), "%s", DEFAULT_DEVICE_HOSTNAME);
}

const char* validateSsid(const char* ssid) {
    if (!ssid) return "Wi-Fi name is missing";
    const size_t n = strlen(ssid);
    if (n == 0) return "Wi-Fi name must not be empty";
    if (n > SSID_MAX) return "Wi-Fi name must be at most 32 characters";
    if (!printable(ssid)) return "Wi-Fi name may only use plain ASCII characters";
    return nullptr;
}

const char* validatePassword(const char* password) {
    if (!password) return "Password is missing";
    const size_t n = strlen(password);
    if (n < PASS_MIN || n > PASS_MAX) return "Password must be 8 to 63 characters";
    if (!printable(password)) return "Password may only use plain ASCII characters";
    return nullptr;
}

const char* validateChannel(int channel) {
    if (channel < CHANNEL_MIN || channel > CHANNEL_MAX) return "Channel must be 1 to 13";
    return nullptr;
}

const char* validateHostname(const char* hostname) {
    if (!hostname) return "mDNS name is missing";
    const size_t n = strlen(hostname);
    if (n == 0) return nullptr;  // fallback responder off
    if (n > HOSTNAME_MAX) return "mDNS name must be at most 63 characters";
    if (hostname[0] == '-' || hostname[n - 1] == '-') {
        return "mDNS name must not start or end with '-'";
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = hostname[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok) return "mDNS name may only use letters, digits and '-'";
    }
    return nullptr;
}

const char* validateSettings(const Settings& s) {
    if (const char* e = validateSsid(s.ssid)) return e;
    if (const char* e = validatePassword(s.password)) return e;
    if (const char* e = validateChannel(s.channel)) return e;
    return validateHostname(s.deviceHostname);
}
