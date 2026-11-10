#pragma once

#include <cstdint>

// Auto-repeat for a held key (pure logic): the first event fires on press,
// then after `delayMs` one repeat every `intervalMs`. After `boostAfter`
// repeats, `boosted()` turns true so the caller can switch to big steps.
class KeyRepeater {
public:
    KeyRepeater(uint32_t delayMs = 380, uint32_t intervalMs = 90, uint32_t boostAfter = 12)
        : delayMs_(delayMs), intervalMs_(intervalMs), boostAfter_(boostAfter) {}

    void press(uint32_t nowMs) {
        held_ = true;
        nextMs_ = nowMs + delayMs_;
        repeats_ = 0;
    }
    void release() { held_ = false; }
    bool held() const { return held_; }

    // Number of repeats due since the last call (0 when not held).
    uint32_t poll(uint32_t nowMs) {
        if (!held_) return 0;
        uint32_t n = 0;
        while ((int32_t)(nowMs - nextMs_) >= 0 && n < 8) {
            ++n;
            ++repeats_;
            nextMs_ += intervalMs_;
        }
        return n;
    }
    bool boosted() const { return repeats_ >= boostAfter_; }

private:
    uint32_t delayMs_;
    uint32_t intervalMs_;
    uint32_t boostAfter_;
    bool held_ = false;
    uint32_t nextMs_ = 0;
    uint32_t repeats_ = 0;
};
