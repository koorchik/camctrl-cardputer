#pragma once

#include <cstddef>
#include <cstdint>

#include "logic/camera_values.h"
#include "logic/keymap.h"

// The remote's UI state and key handling (pure logic). Keys change the
// selection or the shown values right away (optimistic) and yield a command
// for the camera client to send.

enum class RemoteRow : uint8_t { Focus, Iris, Iso, Wb, Tint, Feed, Count };
constexpr int REMOTE_ROWS = (int)RemoteRow::Count;

enum class RemoteCmd : uint8_t {
    None,
    SetFocus,     // f = normalised focus
    AutoFocus,
    IrisStep,     // i = adjustmentStep (+ closes the iris)
    SetIso,       // i = ISO
    SetWb,        // i = Kelvin
    AutoWb,
    SetTint,      // i = tint
    SetCleanFeed, // display, b = enabled
};

struct RemoteCommand {
    RemoteCmd type = RemoteCmd::None;
    float f = 0.0f;
    int i = 0;
    int display = 0;
    bool b = false;
};

// What the key means to the remote (after layout lookup).
enum class RemoteKey : uint8_t {
    None, Up, Down, Left, Right, Enter, AutoFocus, Help, Back,
    JumpFocus, JumpIris, JumpIso, JumpWb, JumpTint, JumpFeed,
    // Overview grid moves (from UiNav, no key of their own).
    ColLeft, ColRight,
};

// The overview is a 2 x 3 grid: Focus Iris Iso | Wb Tint Feed.
constexpr int REMOTE_GRID_ROWS = 3;

RemoteKey remoteKeyFor(const Key& k);
// Left/Right/Up/Down repeat while held.
bool remoteKeyRepeats(RemoteKey k);

// Steps (see the design doc).
constexpr float FOCUS_STEP = 0.005f;
constexpr float FOCUS_BIG_STEP = 0.05f;
constexpr int IRIS_BIG_STEP = 3;
constexpr int ISO_BIG_STEP = 3;
constexpr int WB_STEP = 50;
constexpr int WB_BIG_STEP = 500;
constexpr int TINT_BIG_STEP = 10;

class RemoteModel {
public:
    RemoteRow row() const { return row_; }
    int feedDisplay() const { return feedDisplay_; }
    bool helpVisible() const { return help_; }
    // True when the last Left/Right hit the end of the range (no command).
    bool limitHit() const { return limitHit_; }

    // Applies one key. `big` = shift held or repeat boosted. Returns the
    // command to send (type None if nothing to send).
    RemoteCommand onKey(RemoteKey key, bool big, CameraValues& v);

    // Keeps the selected output valid after the display list changed.
    void clampFeedDisplay(const CameraValues& v);

private:
    RemoteCommand change(int dir, bool big, CameraValues& v);
    RemoteCommand changeValue(int dir, bool big, CameraValues& v);
    bool rowKnown(const CameraValues& v) const;
    RemoteCommand rowAction(CameraValues& v);

    RemoteRow row_ = RemoteRow::Focus;
    int feedDisplay_ = 0;
    bool help_ = false;
    bool limitHit_ = false;
};

// Display helpers (into `out`, NUL-terminated).
void formatFocus(const CameraValues& v, char* out, size_t cap);
void formatIris(const CameraValues& v, char* out, size_t cap);
void formatIso(const CameraValues& v, char* out, size_t cap);
void formatWb(const CameraValues& v, char* out, size_t cap);
void formatTint(const CameraValues& v, char* out, size_t cap);
// Short label for a monitoring display: MainSDI -> SDI, FrontUSBC -> USB.
void shortDisplayName(const char* name, char* out, size_t cap);
