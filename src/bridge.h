#pragma once

#include <cstdint>

#include "esp_err.h"
#include "logic/settings.h"

// The layer-2 bridge: USB NCM (device) <-> Wi-Fi AP (phones) <-> the
// adapter's own IP interface (DHCP, mDNS fallback, settings page).

struct BridgeStatus {
    bool usbMounted;
    bool apStarted;
    int wifiClients;
    uint32_t deviceSeenIp;   // last IPv4 sender address seen on USB, 0 if none
    uint32_t usbToWifi;
    uint32_t wifiToUsb;
    uint32_t toLocal;
    uint32_t fromLocal;
    uint32_t drops;
};

esp_err_t bridgeStart(const Settings& settings);
void bridgeStatus(BridgeStatus& out);

// True when `mac` was last seen on the USB side (the device).
bool bridgeMacOnUsb(const uint8_t mac[6]);
