#pragma once

#include <cstdint>

#include "logic/camera_values.h"
#include "logic/remote_model.h"

// Talks to the camera on the USB side (REST + WebSocket) from the adapter's
// own interface and keeps the values up to date.

enum class CameraPhase : uint8_t {
    NoUsb,       // no USB host has configured the adapter
    WaitingIp,   // USB up, the device has no address in our subnet yet
    Connecting,  // talking to the device, not (yet) a camera answering
    Connected,   // camera answered; values are live
};

enum class OpState : uint8_t { Idle, Running, Done, Timeout };

enum class NoticeKind : uint8_t { Info, Ok, Warn, Error };

struct CameraView {
    CameraValues shown;   // what the remote shows (local changes applied at once)
    CameraValues actual;  // what the camera last reported
    CameraPhase phase = CameraPhase::NoUsb;
    bool live = false;    // WebSocket events flowing

    OpState af = OpState::Idle;
    uint32_t afAt = 0;    // when af last changed state (ms)
    OpState awb = OpState::Idle;
    uint32_t awbAt = 0;
    int irisQueued = 0;   // iris steps requested but not yet confirmed (+ = closing)

    char notice[28] = "";
    NoticeKind noticeKind = NoticeKind::Info;
    uint32_t noticeAt = 0;
    uint32_t failedAt = 0;  // last failed write (ms), for a shake
};

void cameraClientStart();

void cameraSnapshot(CameraView& out);

// Runs `model.onKey()` against the shown values (so optimistic changes are
// not overwritten by stale reports) and queues the resulting command.
RemoteCommand cameraHandleKey(RemoteModel& model, RemoteKey key, bool big);

// A notice raised by the UI itself (e.g. "PRESS h FOR KEYS").
void cameraLocalNotice(const char* text, NoticeKind kind);
