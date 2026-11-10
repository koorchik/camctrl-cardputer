#pragma once

#include <cstdint>

#include "camera/camera_client.h"
#include "logic/remote_model.h"

// The Cardputer screen (240 x 135, M5GFX). Rendering only; no logic.

struct UiState {
    CameraValues cam;
    CameraPhase phase = CameraPhase::NoUsb;
    bool liveEvents = false;

    RemoteRow row = RemoteRow::Focus;
    int feedDisplay = 0;
    bool help = false;

    char ssid[33] = "";
    char password[64] = "";
    int wifiClients = 0;

    char toast[24] = "";        // shown instead of the header while set
    uint32_t resetHeldMs = 0;   // G0 held for factory reset
    bool powerSwitchHint = false;
};

void uiInit();
void uiRender(const UiState& s);
void uiRenderSafeMode(uint32_t secondsLeft, uint32_t resetHeldMs);
