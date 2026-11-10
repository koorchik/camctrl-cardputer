#pragma once

#include <cmath>
#include <cstdint>

// Visual language of the remote (see docs/superpowers/specs/2026-11-10-remote-ui-design.md).
namespace theme {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t STATUS_BG = 0x0862;
constexpr uint16_t PANEL = 0x10A3;
constexpr uint16_t PANEL_HI = 0x2125;
constexpr uint16_t LINE = 0x3A09;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t TEXT_2 = 0xB5B8;
constexpr uint16_t TEXT_3 = 0x632D;
constexpr uint16_t ACCENT = 0xFDE0;
constexpr uint16_t ACCENT_DIM = 0x6A80;
constexpr uint16_t OK = 0x2F0C;
constexpr uint16_t INFO = 0x065F;
constexpr uint16_t WARN = 0xFC00;
constexpr uint16_t ERR = 0xF145;
constexpr uint16_t PVW = 0x0688;
constexpr uint16_t LIGHT = 0xFF78;
constexpr uint16_t BLADE = 0x2987;
constexpr uint16_t TINT_G = 0x2E4A;
constexpr uint16_t TINT_N = 0x4A49;
constexpr uint16_t TINT_M = 0xE199;
constexpr uint16_t SKY = 0x2A90;
constexpr uint16_t GROUND = 0x2AC6;
constexpr uint16_t SUN = 0xFE8A;
constexpr uint16_t SKIN = 0xDD50;

constexpr int LUT_W = 220;
extern uint16_t kelvinLut[LUT_W];  // 2500..10000 K
extern uint16_t tintLut[LUT_W];    // -50..+50

void initLuts();
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t blend(uint16_t a, uint16_t b, float t);
uint16_t kelvinColor(int k);

// Eases `cur` toward `target` (frame-rate independent).
inline float approach(float cur, float target, float dtMs, float halfLifeMs, float snap) {
    cur += (target - cur) * (1.0f - exp2f(-dtMs / halfLifeMs));
    if (fabsf(target - cur) < snap) cur = target;
    return cur;
}
}  // namespace theme
