#include "ui/theme.h"

namespace theme {
uint16_t kelvinLut[LUT_W];
uint16_t tintLut[LUT_W];

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend(uint16_t a, uint16_t b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (int)((br - ar) * t + 0.5f);
    const int g = ag + (int)((bg - ag) * t + 0.5f);
    const int bl = ab + (int)((bb - ab) * t + 0.5f);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

namespace {
struct Stop {
    int k;
    uint8_t r, g, b;
};
// Stylised (more saturated than blackbody) so it reads on a small screen.
const Stop kStops[] = {
    {2500, 255, 120, 20}, {3200, 255, 160, 60}, {4000, 255, 200, 130}, {5600, 255, 246, 236},
    {6500, 225, 236, 255}, {7500, 170, 200, 255}, {10000, 80, 140, 255},
};
}  // namespace

uint16_t kelvinColor(int k) {
    if (k <= kStops[0].k) return rgb(kStops[0].r, kStops[0].g, kStops[0].b);
    for (size_t i = 1; i < sizeof(kStops) / sizeof(kStops[0]); ++i) {
        if (k <= kStops[i].k) {
            const Stop& a = kStops[i - 1];
            const Stop& b = kStops[i];
            const float t = (float)(k - a.k) / (float)(b.k - a.k);
            return rgb((uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
                       (uint8_t)(a.b + (b.b - a.b) * t));
        }
    }
    const Stop& l = kStops[sizeof(kStops) / sizeof(kStops[0]) - 1];
    return rgb(l.r, l.g, l.b);
}

void initLuts() {
    for (int x = 0; x < LUT_W; ++x) {
        kelvinLut[x] = kelvinColor(2500 + x * 7500 / (LUT_W - 1));
        const float t = (float)x / (LUT_W - 1) * 2.0f - 1.0f;  // -1..1
        tintLut[x] = t < 0 ? blend(TINT_N, TINT_G, -t) : blend(TINT_N, TINT_M, t);
    }
}
}  // namespace theme
