#pragma once

#include "logic/settings.h"

// Settings page and small JSON API on http://192.168.73.1/
//   GET  /             status + settings form
//   POST /save         save settings (form), then restart
//   GET  /api/status   JSON status
//   GET  /api/log      recent log text
//   POST /api/reboot?mode=normal|safe|download
//        restart; "safe" and "download" give USB back to serial/JTAG for esptool
//   POST /api/ota      body = firmware.bin; flashes the other slot and restarts
// The two POST /api/ calls require the header "X-EthAir: 1" (CSRF guard).
void webStart(const Settings& current);

// Number of HTTP requests served since boot.
uint32_t webRequestCount();
// True while the running image is a fresh OTA that has not been confirmed.
bool webOtaPending();
