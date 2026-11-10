#pragma once

#include <cstdint>

// Detects a button held for `holdMs`. Pure logic.
class HoldDetector {
public:
    explicit HoldDetector(uint32_t holdMs) : holdMs_(holdMs) {}

    // Feed the current state; returns true exactly once per press, when the
    // hold time is reached.
    bool update(bool pressed, uint32_t nowMs) {
        if (!pressed) {
            down_ = false;
            fired_ = false;
            return false;
        }
        if (!down_) {
            down_ = true;
            sinceMs_ = nowMs;
        }
        if (!fired_ && nowMs - sinceMs_ >= holdMs_) {
            fired_ = true;
            return true;
        }
        return false;
    }

    // Milliseconds the button has been held (0 when released).
    uint32_t heldMs(uint32_t nowMs) const { return down_ ? nowMs - sinceMs_ : 0; }

private:
    uint32_t holdMs_;
    uint32_t sinceMs_ = 0;
    bool down_ = false;
    bool fired_ = false;
};
