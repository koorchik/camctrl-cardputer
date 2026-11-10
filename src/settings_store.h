#pragma once

#include "logic/settings.h"

// Loads settings from NVS, falling back to defaults for anything missing or
// invalid. nvs_flash must be initialised.
void settingsLoad(Settings& out);

// Validates and saves. Returns nullptr on success or an error message.
const char* settingsSave(const Settings& s);

// Erases saved settings (factory reset).
void settingsErase();

// Remote UI flags (erased with the settings on factory reset).
bool settingsUiFlag(const char* key);
void settingsSetUiFlag(const char* key, bool value);

// The MAC used for the default SSID (the Wi-Fi access point MAC).
void settingsDeviceMac(uint8_t mac[6]);
