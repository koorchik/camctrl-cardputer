#pragma once

#include <cstdint>

#include "camera/camera_client.h"
#include "logic/remote_model.h"
#include "logic/ui_nav.h"

// The Cardputer screen (240 x 135, M5GFX). Rendering and animation only;
// what to show comes from UiState. Layout and colours follow
// docs/superpowers/specs/2026-11-10-remote-ui-design.md.

struct UiState {
    uint32_t nowMs = 0;
    CameraView cam;

    UiView view = UiView::Overview;
    RemoteRow row = RemoteRow::Focus;
    int feedDisplay = 0;
    int introCard = 0;
    bool hintsSlim = false;
    bool bigStep = false;          // shift held or repeat boosted
    int isoIndexAtOpen = -1;       // for the EV chip

    char ssid[33] = "";
    char password[64] = "";
    int wifiClients = 0;
    bool powerSwitchHint = false;

    uint32_t resetHeldMs = 0;      // G0 held
    RemoteKey pressedKey = RemoteKey::None;
    uint32_t pressedAt = 0;
    uint32_t limitHitAt = 0;       // value at the end of its range
    uint32_t unavailableAt = 0;    // key on a control that cannot change now
};

void uiInit();
// Draws one frame. Returns true while something is animating (call again
// soon); false when the frame is static.
bool uiRender(const UiState& s);
void uiRenderSafeMode(uint32_t secondsLeft, uint32_t totalSeconds, uint32_t resetHeldMs);
