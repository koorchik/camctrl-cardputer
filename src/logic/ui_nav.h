#pragma once

#include <cstdint>

#include "logic/remote_model.h"

// Which screen the remote shows and how keys move between screens (pure
// logic). The overview shows all six controls in a 2 x 3 grid; the arrows
// move the selection and Enter opens the control's detail view. There
// Left/Right adjust, Up/Down adjust in big steps, Enter is the control's
// action, Back (or a short idle) returns to the overview.

enum class UiView : uint8_t { Overview, Detail, Help, Intro };

constexpr uint32_t DETAIL_IDLE_CLOSE_MS = 3000;
constexpr uint32_t INTRO_AF_ADVANCE_MS = 800;

// Keys to pass on to RemoteModel, in order (at most two).
struct NavKeys {
    RemoteKey keys[2] = {RemoteKey::None, RemoteKey::None};
    int count = 0;
    bool big = false;  // apply as a big step (Up/Down in a detail view)
    void add(RemoteKey k) {
        if (count < 2) keys[count++] = k;
    }
};

class UiNav {
public:
    UiView view() const { return view_; }
    int introCard() const { return introCard_; }
    // Set once the intro was closed by the user (persist it).
    bool introFinished() const { return introFinished_; }

    void startIntro();
    // Decides the screen change for one key press and which keys the remote
    // model must apply. `connected`: a camera is connected.
    NavKeys onKey(RemoteKey k, uint32_t nowMs, bool connected);

    // Closes an idle detail view. `busy`: a key is held, AF/auto WB runs or
    // a value is still pending.
    void tick(uint32_t nowMs, bool busy);

    // Autofocus finished while the intro's first card waits for it.
    void onAutofocusDone(uint32_t nowMs);

private:
    UiView view_ = UiView::Overview;
    int introCard_ = 0;
    bool introFinished_ = false;
    uint32_t lastKeyMs_ = 0;
    uint32_t introAdvanceAt_ = 0;
};

// Graduates the hint bar from FULL to SLIM once the user knows the keys,
// and back to FULL when they seem lost (pure logic).
class HintTracker {
public:
    explicit HintTracker(bool slim) : slim_(slim) {}
    bool slim() const { return slim_ && !lostThisSession_; }
    // True once after the user graduated (persist it).
    bool takeGraduated() {
        const bool g = graduated_;
        graduated_ = false;
        return g;
    }

    void onNavigate() { ++navigations_; check(); }
    void onConfirmedAdjust() { ++adjustments_; check(); }
    void onAutofocus() { ++autofocus_; check(); }
    // A key with no meaning here. Returns true if the user seems lost
    // (2 within 5 s): show the "press h" banner.
    bool onUnmappedKey(uint32_t nowMs);
    void reset() {
        slim_ = false;
        lostThisSession_ = false;
        navigations_ = adjustments_ = autofocus_ = 0;
    }

private:
    void check() {
        if (!slim_ && navigations_ >= 3 && adjustments_ >= 5 && autofocus_ >= 1) {
            slim_ = true;
            graduated_ = true;
        }
    }

    bool slim_;
    bool graduated_ = false;
    bool lostThisSession_ = false;
    int navigations_ = 0;
    int adjustments_ = 0;
    int autofocus_ = 0;
    uint32_t lastUnmappedMs_ = 0;
    bool hadUnmapped_ = false;
};
