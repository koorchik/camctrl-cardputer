#pragma once

#include <cstdint>

#include "logic/camera_values.h"
#include "logic/remote_model.h"

// Talks to the camera on the USB side (REST + WebSocket) from the adapter's
// own interface and keeps a CameraValues copy up to date.

enum class CameraPhase : uint8_t {
    NoUsb,       // no USB host has configured the adapter
    WaitingIp,   // USB up, the device has no address in our subnet yet
    Connecting,  // talking to the device, not (yet) a camera answering
    Connected,   // camera answered; values are live
};

void cameraClientStart();

// Copy of the current values and phase.
void cameraSnapshot(CameraValues& out, CameraPhase& phase, bool& liveEvents);

// Runs `model.onKey()` against the shared values (so optimistic changes are
// not overwritten by stale reports) and queues the resulting command.
RemoteCommand cameraHandleKey(RemoteModel& model, RemoteKey key, bool big);

// Last short notice for the UI ("AUTOFOCUS", "WRITE FAILED", ...) and when
// it was raised (ms since boot); empty if none.
void cameraNotice(char* out, size_t cap, uint32_t& atMs);
