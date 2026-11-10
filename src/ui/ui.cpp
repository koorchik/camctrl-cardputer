#include "ui/ui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>

#include "button.h"
#include "ui/canvas.h"
#include "ui/theme.h"

using namespace theme;

namespace {
// --- display ----------------------------------------------------------------
// Cardputer ADV: ST7789V2 135 x 240 on SPI3, used in landscape. Explicit
// config (board autodetect would probe the I2C bus the keyboard owns).
class CardputerDisplay : public lgfx::LGFX_Device {
public:
    CardputerDisplay() {
        auto b = bus_.config();
        b.spi_host = SPI3_HOST;
        b.spi_mode = 0;
        b.freq_write = 40000000;
        b.freq_read = 16000000;
        b.spi_3wire = true;
        b.use_lock = true;
        b.dma_channel = SPI_DMA_CH_AUTO;
        b.pin_sclk = 36;
        b.pin_mosi = 35;
        b.pin_miso = -1;
        b.pin_dc = 34;
        bus_.config(b);
        panel_.setBus(&bus_);
        auto p = panel_.config();
        p.pin_cs = 37;
        p.pin_rst = 33;
        p.pin_busy = -1;
        p.panel_width = 135;
        p.panel_height = 240;
        p.offset_x = 52;
        p.offset_y = 40;
        p.offset_rotation = 0;
        p.readable = false;
        p.invert = true;
        p.rgb_order = false;
        p.dlen_16bit = false;
        p.bus_shared = false;
        panel_.config(p);
        // GPIO38 is the backlight and also powers the RGB LED: never low.
        auto l = light_.config();
        l.pin_bl = 38;
        l.invert = false;
        l.freq = 256;
        l.pwm_channel = 7;
        light_.config(l);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }

private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

constexpr int W = 240;
constexpr int H = 135;
constexpr int BAND_H = 68;  // two bands: 0..67, 68..134

CardputerDisplay s_lcd;
LGFX_Sprite* s_sprite = nullptr;
Canvas* s_c = nullptr;

const lgfx::IFont* F0 = &fonts::Font0;
const lgfx::IFont* F2 = &fonts::Font2;
const lgfx::IFont* B9 = &fonts::FreeSansBold9pt7b;
const lgfx::IFont* R9 = &fonts::FreeSans9pt7b;
const lgfx::IFont* B12 = &fonts::FreeSansBold12pt7b;
const lgfx::IFont* B18 = &fonts::FreeSansBold18pt7b;
const lgfx::IFont* B24 = &fonts::FreeSansBold24pt7b;
const lgfx::IFont* F7 = &fonts::Font7;

// --- animation state ----------------------------------------------------------
struct Anim {
    uint32_t lastMs = 0;
    bool active = false;  // something moved this frame
    float focusDisp = -1000;  // 0..100
    float irisDisp = -1000;   // f-stop
    float isoIdxDisp = -1000;
    float wbDisp = -1000;
    float tintDisp = -1000;
    float selX = -1000, selY = -1000;
    float feedSelX = -1000;
    float overlay[CAM_MAX_DISPLAYS] = {1, 1, 1, 1};  // 1 = overlay bars shown
    uint32_t confirmAt[REMOTE_ROWS] = {};
    uint32_t externalAt[REMOTE_ROWS] = {};
    bool pendingPrev[REMOTE_ROWS] = {};
    CameraValues actualPrev;
    bool havePrev = false;
    int tintSide = 0;
    uint32_t detentAt = 0;
} A;

// Eases toward target; the first call (cur <= -900) jumps.
float ease(float cur, float target, float dt, float halfLife, float snap) {
    if (cur <= -900) return target;
    const float n = approach(cur, target, dt, halfLife, snap);
    if (n != target) A.active = true;
    return n;
}

bool within(uint32_t now, uint32_t at, uint32_t ms) {
    return at && now - at < ms;
}

float flashT(uint32_t now, uint32_t at, uint32_t ms) {  // 0 at start -> 1 done
    if (!at || now - at >= ms) return 1.0f;
    A.active = true;
    return (float)(now - at) / (float)ms;
}

int shakeOffset(uint32_t now, uint32_t at) {
    static const int off[] = {3, -3, 2, -2, 0};
    if (!at || now - at >= 170) return 0;
    A.active = true;
    return off[((now - at) / 34) % 5];
}

bool blink(uint32_t now, uint32_t period) {
    A.active = true;
    return (now / period) % 2 == 0;
}

const char* rowLabel(RemoteRow r) {
    static const char* const labels[] = {"FOCUS", "IRIS", "ISO", "WB", "TINT", "CLEAN FEED"};
    return labels[(int)r];
}

const char* rowTitle(RemoteRow r) {
    static const char* const titles[] = {"FOCUS", "IRIS", "ISO", "WHITE BALANCE", "TINT", "CLEAN FEED"};
    return titles[(int)r];
}

char jumpLetter(RemoteRow r) {
    static const char letters[] = {'f', 'i', 'o', 'w', 't', 'c'};
    return letters[(int)r];
}

// Pending = a local change the camera has not reported back yet.
bool rowPending(const CameraView& cv, RemoteRow r) {
    const CameraValues& s = cv.shown;
    const CameraValues& a = cv.actual;
    switch (r) {
        case RemoteRow::Focus: return s.focusKnown && fabsf(s.focus - a.focus) > 0.01f;
        case RemoteRow::Iris: return cv.irisQueued != 0;
        case RemoteRow::Iso: return s.isoKnown && s.iso != a.iso;
        case RemoteRow::Wb: return s.wbKnown && s.wb != a.wb;
        case RemoteRow::Tint: return s.tintKnown && s.tint != a.tint;
        case RemoteRow::Feed:
            for (size_t i = 0; i < s.displayCount; ++i) {
                if (s.cleanFeed[i] != a.cleanFeed[i]) return true;
            }
            return false;
        default: return false;
    }
}

bool rowUnavailable(const CameraValues& v, RemoteRow r) {
    switch (r) {
        case RemoteRow::Focus: return !v.focusControllable;
        case RemoteRow::Iris: return !v.irisControllable || v.irisAuto || (v.irisKnown && v.irisStop <= 0);
        default: return false;
    }
}

bool rowKnown(const CameraValues& v, RemoteRow r) {
    switch (r) {
        case RemoteRow::Focus: return v.focusKnown;
        case RemoteRow::Iris: return v.irisKnown && v.irisStop > 0;
        case RemoteRow::Iso: return v.isoKnown;
        case RemoteRow::Wb: return v.wbKnown;
        case RemoteRow::Tint: return v.tintKnown;
        case RemoteRow::Feed: return v.displayCount > 0;
        default: return false;
    }
}

// Value colour: amber while pending, info flash after an external change.
uint16_t valueColor(const UiState& s, RemoteRow r) {
    const CameraValues& v = s.cam.shown;
    if (!rowKnown(v, r)) return TEXT_3;
    if (rowUnavailable(v, r)) return (r == RemoteRow::Iris && v.irisAuto) ? TEXT_2 : TEXT_3;
    if (rowPending(s.cam, r)) return ACCENT;
    const float ext = flashT(s.nowMs, A.externalAt[(int)r], 400);
    if (ext < 1) return blend(INFO, TEXT, ext);
    return TEXT;
}

void irisText(const CameraValues& v, char* out, size_t cap) {
    float f = v.irisStop;
    if (v.irisMin > 0 && fabsf(f - v.irisMin) < 0.1f) f = v.irisMin;
    if (v.irisMax > 0 && fabsf(f - v.irisMax) < 0.1f) f = v.irisMax;
    const float r = roundf(f * 10.0f) / 10.0f;
    if (fabsf(r - roundf(r)) < 0.05f) {
        snprintf(out, cap, "%d", (int)roundf(r));
    } else {
        snprintf(out, cap, "%.1f", r);
    }
}

void focusText(float focus, char* out, size_t cap) {
    snprintf(out, cap, "%.1f", focus * 100.0f);
}

// --- components -------------------------------------------------------------
enum class Arrow : uint8_t { None, Up, Down, Left, Right };
enum class CapStyle : uint8_t { Normal, Dim, Pressed };

void drawArrow(int x, int y, Arrow a, uint16_t c) {
    switch (a) {
        case Arrow::Up: s_c->fillTriangle(x, y + 4, x + 4, y + 4, x + 2, y, c); break;
        case Arrow::Down: s_c->fillTriangle(x, y, x + 4, y, x + 2, y + 4, c); break;
        case Arrow::Left: s_c->fillTriangle(x + 4, y, x + 4, y + 4, x, y + 2, c); break;
        case Arrow::Right: s_c->fillTriangle(x, y, x, y + 4, x + 4, y + 2, c); break;
        default: break;
    }
}

int keycapWidth(const char* legend, Arrow a) {
    return 6 * (int)strlen(legend) + 6 + (a != Arrow::None ? 7 : 0);
}

int drawKeycap(int x, int y, const char* legend, Arrow a, CapStyle st) {
    const int w = keycapWidth(legend, a);
    const uint16_t fill = st == CapStyle::Pressed ? ACCENT : (st == CapStyle::Dim ? STATUS_BG : PANEL_HI);
    const uint16_t text = st == CapStyle::Pressed ? BG : (st == CapStyle::Dim ? TEXT_2 : TEXT);
    const uint16_t arrow = st == CapStyle::Pressed ? BG : (st == CapStyle::Dim ? ACCENT_DIM : ACCENT);
    s_c->fillRoundRect(x, y, w, 11, 2, fill);
    s_c->drawRoundRect(x, y, w, 11, 2, LINE);
    s_c->drawFastHLine(x + 2, y + 11, w - 4, LINE);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->setTextColor(text);
    s_c->drawString(legend, x + 3, y + 2);
    if (a != Arrow::None) drawArrow(x + 3 + 6 * (int)strlen(legend) + 2, y + 3, a, arrow);
    return w;
}

// Chip right-aligned at xRight; returns its left x.
int drawChip(int xRight, int y, const char* text, uint16_t color, bool filled) {
    s_c->setFont(F0);
    const int w = s_c->textWidth(text) + 8;
    const int x = xRight - w;
    if (filled) {
        s_c->fillRoundRect(x, y, w, 11, 3, color);
        s_c->setTextColor(color == ERR ? TEXT : BG);
    } else {
        s_c->drawRoundRect(x, y, w, 11, 3, color);
        s_c->setTextColor(color);
    }
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->drawString(text, x + 4, y + 2);
    return x;
}

void drawPadlock(int x, int y, uint16_t c) {
    s_c->drawArc(x + 4, y + 4, 3, 2, 180, 360, c);
    s_c->fillRoundRect(x + 1, y + 4, 7, 5, 1, c);
}

void markerDown(int x, int apexY, uint16_t c) {
    s_c->fillTriangle(x - 5, apexY - 8, x + 5, apexY - 8, x, apexY, c);
}

void markerUp(int x, int apexY, uint16_t c) {
    s_c->fillTriangle(x - 5, apexY + 7, x + 5, apexY + 7, x, apexY, c);
}

void drawAfFrame(int cx, int cy, int s, int arm, int thick, uint16_t c) {
    s_c->fillRect(cx - s, cy - s, arm, thick, c);
    s_c->fillRect(cx - s, cy - s, thick, arm, c);
    s_c->fillRect(cx + s - arm + 1, cy - s, arm, thick, c);
    s_c->fillRect(cx + s - thick + 1, cy - s, thick, arm, c);
    s_c->fillRect(cx - s, cy + s - thick + 1, arm, thick, c);
    s_c->fillRect(cx - s, cy + s - arm + 1, thick, arm, c);
    s_c->fillRect(cx + s - arm + 1, cy + s - thick + 1, arm, thick, c);
    s_c->fillRect(cx + s - thick + 1, cy + s - arm + 1, thick, arm, c);
}

// The status bar is STATUS_H tall; everything below it is drawn shifted
// down by CONTENT_SHIFT (the layouts were drawn for a 14 px bar and a bottom
// key strip, which is gone).
constexpr int STATUS_H = 18;
constexpr int CONTENT_SHIFT = 5;

// Key caps (help screen) ------------------------------------------------------
struct HintCap {
    const char* legend;
    Arrow arrow;
    RemoteKey key;
};
constexpr HintCap CAP_UP = {";", Arrow::Up, RemoteKey::Up};
constexpr HintCap CAP_DOWN = {".", Arrow::Down, RemoteKey::Down};
constexpr HintCap CAP_LEFT = {",", Arrow::Left, RemoteKey::Left};
constexpr HintCap CAP_RIGHT = {"/", Arrow::Right, RemoteKey::Right};
constexpr HintCap CAP_SPC = {"SPC", Arrow::None, RemoteKey::AutoFocus};
constexpr HintCap CAP_ENT = {"ENT", Arrow::None, RemoteKey::Enter};
constexpr HintCap CAP_SHIFT = {"SHIFT", Arrow::None, RemoteKey::None};
constexpr HintCap CAP_H = {"h", Arrow::None, RemoteKey::Help};
constexpr HintCap CAP_G0 = {"G0", Arrow::None, RemoteKey::None};
constexpr HintCap CAP_DEL = {"DEL", Arrow::None, RemoteKey::Back};

// Why a control cannot be changed, in the bottom strip of a detail view.
void drawHintMessage(const char* msg) {
    s_c->setShift(0);
    s_c->fillRect(0, 120, W, 15, BG);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(WARN);
    s_c->drawString(msg, W / 2, 120);
    s_c->setShift(CONTENT_SHIFT);
}

// Status bar, banner, tally ----------------------------------------------------
void shortCameraName(const char* name, char* out, size_t cap) {
    char tmp[CAM_NAME_MAX + 1];
    snprintf(tmp, sizeof(tmp), "%s", name[0] ? name : "CAMERA");
    char* p = strstr(tmp, " Camera");
    if (p) memmove(p, p + 7, strlen(p + 7) + 1);
    snprintf(out, cap, "%s", tmp);
}

// Camera power, right-aligned at xRight (status bar, F2): percentage, a
// battery icon with its level, a bolt while charging; the power source when
// there is no battery. Returns the left x (xRight when nothing is known).
int drawCameraPower(const CameraValues& v, int xRight) {
    if (!v.powerKnown) return xRight;
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_right);
    if (!v.batteryPresent) {
        const char* src = v.powerSource == CameraValues::PowerSource::Ac    ? "AC"
                          : v.powerSource == CameraValues::PowerSource::Usb ? "USB"
                          : v.powerSource == CameraValues::PowerSource::Battery ? "BATT"
                                                                                : "DC";
        s_c->setTextColor(TEXT_2);
        return xRight - s_c->drawString(src, xRight, 1);
    }
    // Icon: 20 x 11 body plus a 2 x 5 nub on the right.
    const int bx = xRight - 22, by = 3;
    const int pct = v.batteryPercent < 0 ? -1 : (v.batteryPercent > 100 ? 100 : v.batteryPercent);
    const uint16_t level = v.batteryCharging ? INFO : (pct >= 0 && pct <= 15 ? ERR : (pct >= 0 && pct <= 30 ? WARN : OK));
    s_c->drawRoundRect(bx, by, 20, 11, 2, TEXT_2);
    s_c->fillRect(bx + 20, by + 3, 2, 5, TEXT_2);
    if (pct >= 0) s_c->fillRect(bx + 2, by + 2, (16 * pct + 50) / 100, 7, level);
    if (v.batteryCharging) {
        // Lightning bolt over the level bar.
        const int cx = bx + 10;
        s_c->fillTriangle(cx + 2, by - 1, cx - 3, by + 6, cx, by + 6, TEXT);
        s_c->fillTriangle(cx, by + 4, cx + 3, by + 4, cx - 2, by + 12, TEXT);
    }
    int x = bx - 3;
    if (pct >= 0) {
        char t[8];
        snprintf(t, sizeof(t), "%d%%", pct);
        s_c->setTextColor(pct <= 15 && !v.batteryCharging ? ERR : TEXT);
        x -= s_c->drawString(t, x, 1);
    }
    return x;
}

// Top bar (STATUS_H tall, F2 text): REC / tally chips and the camera name on
// the left; battery, live dot, phones and the help key on the right.
void drawStatusBar(const UiState& s, bool camera) {
    s_c->fillRect(0, 0, W, STATUS_H, STATUS_BG);
    s_c->setFont(F2);

    // Help key, drawn as a key cap.
    int rx = W - 3;
    s_c->fillRoundRect(rx - 15, 2, 15, 14, 3, PANEL_HI);
    s_c->drawRoundRect(rx - 15, 2, 15, 14, 3, LINE);
    s_c->setTextDatum(textdatum_t::middle_center);
    s_c->setTextColor(TEXT);
    s_c->drawString("h", rx - 7, 8);
    rx -= 21;

    if (s.wifiClients > 0) {
        char n[12];
        snprintf(n, sizeof(n), "%d", s.wifiClients);
        s_c->setTextDatum(textdatum_t::top_right);
        s_c->setTextColor(TEXT_2);
        rx -= s_c->drawString(n, rx, 1) + 2;
        // Phone outline.
        s_c->drawRoundRect(rx - 7, 3, 7, 12, 2, TEXT_2);
        s_c->drawFastHLine(rx - 5, 12, 3, TEXT_2);
        rx -= 13;
    }
    if (camera) {
        // Live link: steady green; polling: blinking amber.
        const bool live = s.cam.live;
        if (live || blink(s.nowMs, 500)) s_c->fillCircle(rx - 4, 9, 4, live ? OK : WARN);
        if (!live) A.active = true;
        rx -= 14;
        rx = drawCameraPower(s.cam.actual, rx) - 6;
    }

    int x = 3;
    const CameraValues& v = s.cam.actual;
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::middle_left);
    if (camera && v.recordKnown && v.recording) {
        s_c->fillRoundRect(x, 2, 40, 14, 3, ERR);
        s_c->fillCircle(x + 7, 9, 3, TEXT);
        s_c->setTextColor(TEXT);
        s_c->drawString("REC", x + 13, 9);
        x += 44;
    }
    if (camera && v.tallyKnown && v.tally != CameraValues::Tally::None) {
        const bool pgm = v.tally == CameraValues::Tally::Program;
        s_c->fillRoundRect(x, 2, 36, 14, 3, pgm ? ERR : PVW);
        s_c->setTextColor(pgm ? TEXT : BG);
        s_c->drawString(pgm ? "PGM" : "PVW", x + 4, 9);
        x += 40;
    }
    char name[CAM_NAME_MAX + 1];
    if (camera) {
        shortCameraName(s.cam.shown.name, name, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "CamCtrl");
    }
    s_c->setTextColor(camera ? TEXT_2 : TEXT);
    if (rx - 2 > x) {
        s_c->setClipRect(x, 0, rx - 2 - x, STATUS_H);
        s_c->drawString(name, x + 1, 9);
        s_c->clearClipRect();
    }
}

void drawBanner(const UiState& s) {
    const CameraView& cv = s.cam;
    if (!cv.noticeAt || !cv.notice[0]) return;
    const uint32_t dur = cv.noticeKind == NoticeKind::Error ? 2500 : 1500;
    if (s.nowMs - cv.noticeAt >= dur) return;
    A.active = true;
    const uint32_t age = s.nowMs - cv.noticeAt;
    const int y = age >= 80 ? 0 : -18 + (int)(18.0f * age / 80.0f);
    uint16_t fill = INFO, text = BG;
    switch (cv.noticeKind) {
        case NoticeKind::Ok: fill = OK; break;
        case NoticeKind::Warn: fill = WARN; break;
        case NoticeKind::Error: fill = ERR; text = TEXT; break;
        default: break;
    }
    s_c->fillRect(0, y, W, 18, fill);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::middle_center);
    s_c->setTextColor(text);
    s_c->drawString(cv.notice, W / 2, y + 9);
}

void drawTallyBorder(const UiState& s) {
    const CameraValues& v = s.cam.actual;
    uint16_t c = 0;
    if ((v.recordKnown && v.recording) || (v.tallyKnown && v.tally == CameraValues::Tally::Program)) {
        c = ERR;
    } else if (v.tallyKnown && v.tally == CameraValues::Tally::Preview) {
        c = PVW;
    }
    if (!c) return;
    s_c->drawRect(0, 0, W, H, c);
    s_c->drawRect(1, 1, W - 2, H - 2, c);
}

// --- overview -----------------------------------------------------------------
struct TilePos {
    int x, y;
};
constexpr TilePos kTiles[REMOTE_ROWS] = {{3, 15}, {3, 51}, {3, 87}, {122, 15}, {122, 51}, {122, 87}};
constexpr int TILE_W = 115;
constexpr int TILE_H = 33;

void drawIrisGlyph(int gx, int gy, const CameraValues& v) {
    s_c->fillCircle(gx + 8, gy + 8, 7, BLADE);
    if (!(v.irisKnown && v.irisStop > 0)) return;
    const float ratio = v.irisMin > 0 ? v.irisMin / v.irisStop : 0.5f;
    const float open = 1.5f + 5.5f * ratio;
    const uint16_t c = v.irisAuto ? TEXT_3 : LIGHT;
    for (int k = 0; k < 6; ++k) {
        const float a0 = k * (float)M_PI / 3, a1 = (k + 1) * (float)M_PI / 3;
        s_c->fillTriangle(gx + 8, gy + 8, gx + 8 + (int)lroundf(open * cosf(a0)), gy + 8 + (int)lroundf(open * sinf(a0)),
                          gx + 8 + (int)lroundf(open * cosf(a1)), gy + 8 + (int)lroundf(open * sinf(a1)), c);
    }
}

void drawFeedPills(const UiState& s, int x, int y, int w, int h, bool selected) {
    const CameraValues& v = s.cam.shown;
    const int n = (int)v.displayCount;
    for (int i = 0; i < n && i < 4; ++i) {
        char name[8];
        shortDisplayName(v.displays[i], name, sizeof(name));
        const int px = x + i * (w + 3);
        const bool known = v.cleanFeedKnown[i];
        const bool on = known && v.cleanFeed[i];
        const bool pending = v.cleanFeed[i] != s.cam.actual.cleanFeed[i];
        if (pending) {
            s_c->fillRoundRect(px, y, w, h, 3, ACCENT);
        } else if (on) {
            s_c->fillRoundRect(px, y, w, h, 3, OK);
        } else {
            s_c->drawRoundRect(px, y, w, h, 3, LINE);
        }
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::middle_center);
        s_c->setTextColor(pending || on ? BG : (known ? TEXT_2 : TEXT_3));
        s_c->drawString(name, px + w / 2, y + h / 2);
        if (selected && i == s.feedDisplay) s_c->fillRect(px, y + h + 2, w, 1, ACCENT);
    }
}

void tileValueText(const CameraValues& v, RemoteRow r, char* val, size_t cap) {
    snprintf(val, cap, "--");
    if (!rowKnown(v, r)) return;
    switch (r) {
        case RemoteRow::Focus: {
            char t[12];
            focusText(v.focus, t, sizeof(t));
            snprintf(val, cap, "%s%%", t);
            break;
        }
        case RemoteRow::Iris: {
            char t[12];
            irisText(v, t, sizeof(t));
            snprintf(val, cap, "f/%s", t);
            break;
        }
        case RemoteRow::Iso: snprintf(val, cap, "%d", v.iso); break;
        case RemoteRow::Wb: snprintf(val, cap, "%dK", v.wb); break;
        case RemoteRow::Tint:
            if (v.tint == 0) {
                snprintf(val, cap, "0");
            } else {
                snprintf(val, cap, "%+d", v.tint);
            }
            break;
        default: break;
    }
}

void drawTile(const UiState& s, RemoteRow r) {
    const CameraValues& v = s.cam.shown;
    const int tx = kTiles[(int)r].x, ty = kTiles[(int)r].y;
    const bool sel = s.row == r;
    s_c->fillRoundRect(tx, ty, TILE_W, TILE_H, 4, sel ? PANEL_HI : PANEL);
    const float ext = flashT(s.nowMs, A.externalAt[(int)r], 600);
    if (!sel && ext < 1) s_c->drawRoundRect(tx, ty, TILE_W, TILE_H, 4, blend(INFO, PANEL, ext));

    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->setTextColor(sel ? ACCENT : TEXT_2);
    const int lw = s_c->drawString(rowLabel(r), tx + 6, ty + 3);
    const int kx = tx + 6 + lw + 4;
    s_c->drawRoundRect(kx, ty + 2, 9, 10, 2, LINE);
    const char letter[2] = {jumpLetter(r), 0};
    s_c->setTextColor(TEXT_3);
    s_c->drawString(letter, kx + 2, ty + 3);

    if (r == RemoteRow::Focus && s.cam.af == OpState::Running) {
        if (blink(s.nowMs, 250)) drawChip(tx + 110, ty + 2, "AF", INFO, true);
    } else if (r == RemoteRow::Focus && !v.focusControllable) {
        drawPadlock(tx + 100, ty + 2, WARN);
    } else if (r == RemoteRow::Iris && v.irisKnown && v.irisStop <= 0) {
        drawChip(tx + 110, ty + 2, "NO LENS", TEXT_3, false);
    } else if (r == RemoteRow::Iris && v.irisAuto) {
        drawChip(tx + 110, ty + 2, "AUTO", INFO, false);
    } else if (r == RemoteRow::Iris && !v.irisControllable) {
        drawPadlock(tx + 100, ty + 2, WARN);
    }

    if (r == RemoteRow::Feed) {
        if (v.displayCount) {
            drawFeedPills(s, tx + 6, ty + 14, v.displayCount > 3 ? 24 : 33, 13, sel);
        } else {
            s_c->setFont(B12);
            s_c->setTextDatum(textdatum_t::baseline_right);
            s_c->setTextColor(TEXT_3);
            s_c->drawString("--", tx + 109, ty + 26);
        }
        return;
    }
    const int gx = tx + 6, gy = ty + 12;
    if (r == RemoteRow::Focus) {
        const uint16_t c = s.cam.af == OpState::Running ? INFO : (s.cam.af == OpState::Done ? OK : TEXT_3);
        drawAfFrame(gx + 8, gy + 8, 6, 4, 1, c);
        s_c->fillRect(gx + 7, gy + 7, 2, 2, c);
    } else if (r == RemoteRow::Iris) {
        drawIrisGlyph(gx, gy, v);
    } else if (r == RemoteRow::Wb && v.wbKnown) {
        s_c->fillCircle(gx + 7, gy + 7, 5, kelvinColor(v.wb));
    }

    char val[16];
    tileValueText(v, r, val, sizeof(val));
    s_c->setFont(B12);
    if (s_c->textWidth(val) > 84) s_c->setFont(B9);
    s_c->setTextDatum(textdatum_t::baseline_right);
    const int shake = sel ? shakeOffset(s.nowMs, s.unavailableAt) + shakeOffset(s.nowMs, s.cam.failedAt) : 0;
    s_c->setTextColor(sel && within(s.nowMs, s.cam.failedAt, 300) ? ERR : valueColor(s, r));
    s_c->drawString(val, tx + 109 + shake, ty + 26);

    if (!rowKnown(v, r)) return;
    const int x0 = tx + 6, y0 = ty + 29;
    const float conf = flashT(s.nowMs, A.confirmAt[(int)r], 300);
    const uint16_t track = conf < 1 ? blend(OK, LINE, conf) : LINE;
    switch (r) {
        case RemoteRow::Focus:
            s_c->fillRect(x0, y0, 103, 2, track);
            s_c->fillRect(x0 + (int)(v.focus * 102) - 1, ty + 28, 3, 4, TEXT);
            break;
        case RemoteRow::Iris: {
            s_c->fillRect(x0, y0, 103, 2, track);
            const float n = v.irisNormalised < 0 ? 0 : (v.irisNormalised > 1 ? 1 : v.irisNormalised);
            s_c->fillRect(x0 + (int)(n * 102) - 1, ty + 28, 3, 4, TEXT);
            break;
        }
        case RemoteRow::Iso: {
            const int n = (int)v.isoCount;
            int cur = 0;
            for (int i = 0; i < n; ++i) {
                if (abs(v.isoList[i] - v.iso) < abs(v.isoList[cur] - v.iso)) cur = i;
            }
            const float pitch = n > 1 ? 102.0f / (n - 1) : 0;
            for (int i = 0; i < n; ++i) s_c->fillRect(x0 + (int)(i * pitch), y0, 1, 2, track);
            if (n) s_c->fillRect(x0 + (int)(cur * pitch) - 1, ty + 28, 3, 4, TEXT);
            break;
        }
        case RemoteRow::Wb: {
            for (int i = 0; i < 103; ++i) s_c->drawFastVLine(x0 + i, y0, 2, kelvinLut[i * (LUT_W - 1) / 102]);
            const int mx = x0 + (int)((v.wb - 2500) * 102.0f / 7500.0f);
            s_c->fillRect(mx - 2, ty + 28, 5, 4, BG);
            s_c->fillRect(mx - 1, ty + 28, 3, 4, TEXT);
            break;
        }
        case RemoteRow::Tint: {
            for (int i = 0; i < 103; ++i) s_c->drawFastVLine(x0 + i, y0, 2, tintLut[i * (LUT_W - 1) / 102]);
            s_c->fillRect(x0 + 51, ty + 28, 1, 4, TEXT_2);
            const int mx = x0 + (int)((v.tint + 50) * 102.0f / 100.0f);
            s_c->fillRect(mx - 1, ty + 28, 3, 4, TEXT);
            break;
        }
        default: break;
    }
}

void drawOverview(const UiState& s, float dt) {
    for (int i = 0; i < REMOTE_ROWS; ++i) drawTile(s, (RemoteRow)i);
    const TilePos& t = kTiles[(int)s.row];
    A.selX = ease(A.selX, (float)t.x, dt, 30, 0.25f);
    A.selY = ease(A.selY, (float)t.y, dt, 30, 0.25f);
    const int fx = (int)lroundf(A.selX), fy = (int)lroundf(A.selY);
    s_c->drawRoundRect(fx, fy, TILE_W, TILE_H, 4, ACCENT);
    s_c->drawRoundRect(fx + 1, fy + 1, TILE_W - 2, TILE_H - 2, 4, ACCENT);
}

// --- detail views ---------------------------------------------------------------
void detailTitle(const UiState& s) {
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->setTextColor(ACCENT);
    s_c->drawString(rowTitle(s.row), 6, 16);
}

int drawHero(const UiState& s, const char* prefix, const char* value, const char* unit, uint16_t color) {
    int x = 6 + shakeOffset(s.nowMs, s.unavailableAt) + shakeOffset(s.nowMs, s.cam.failedAt);
    const int x0 = x;
    s_c->setTextDatum(textdatum_t::baseline_left);
    s_c->setTextColor(within(s.nowMs, s.cam.failedAt, 300) ? ERR : color);
    if (prefix) {
        s_c->setFont(B12);
        x += s_c->drawString(prefix, x, 66) + 1;
    }
    s_c->setFont(B24);
    x += s_c->drawString(value, x, 66);
    if (unit) {
        s_c->setFont(B12);
        x += 2;
        x += s_c->drawString(unit, x, 66);
    }
    const float conf = flashT(s.nowMs, A.confirmAt[(int)s.row], 300);
    if (conf < 1) s_c->fillRect(x0, 69, x - x0, 2, blend(OK, BG, conf));
    return x;
}

int stepChip(const UiState& s, int xRight, const char* text) {
    if (!s.bigStep) return xRight;
    return drawChip(xRight, 18, text, ACCENT, true) - 4;
}

void drawFocusDetail(const UiState& s, float dt) {
    const CameraView& cv = s.cam;
    const CameraValues& v = cv.shown;
    detailTitle(s);
    int cx = stepChip(s, 234, "x10");
    if (!v.focusControllable) {
        cx = drawChip(cx, 18, "LOCKED", WARN, false) - 4;
    } else if (cv.af == OpState::Running) {
        if (blink(s.nowMs, 250)) cx = drawChip(cx, 18, "AF", INFO, true) - 4;
    } else if (cv.af == OpState::Done) {
        cx = drawChip(cx, 18, "AF OK", OK, true) - 4;
    } else if (cv.af == OpState::Timeout) {
        cx = drawChip(cx, 18, "AF DONE", TEXT, false) - 4;
    } else {
        cx = drawChip(cx, 18, "MF", TEXT_2, false) - 4;
    }

    char val[12] = "--";
    if (v.focusKnown) focusText(v.focus, val, sizeof(val));
    drawHero(s, nullptr, val, v.focusKnown ? "%" : nullptr, valueColor(s, RemoteRow::Focus));

    // AF viewfinder.
    const bool afRun = cv.af == OpState::Running;
    s_c->drawRoundRect(160, 30, 72, 40, 3, afRun ? INFO : LINE);
    s_c->drawFastHLine(194, 50, 5, TEXT_3);
    s_c->drawFastVLine(196, 48, 5, TEXT_3);
    int fs = 10;
    uint16_t fc = TEXT_3;
    if (afRun) {
        fs = 18 - (int)(9 * ((s.nowMs - cv.afAt) % 400) / 400.0f);
        fc = INFO;
        A.active = true;
    } else if (cv.af == OpState::Done) {
        fs = 9;
        fc = OK;
    } else if (cv.af == OpState::Timeout) {
        fs = 9;
        fc = TEXT;
    }
    drawAfFrame(196, 50, fs, 6, 2, fc);

    // Focus tape: 1 % = 6 px under a fixed index at x = 120.
    const float target = v.focusKnown ? v.focus * 100.0f : 50.0f;
    A.focusDisp = ease(A.focusDisp, target, dt, 35, 0.01f);
    const float vd = A.focusDisp;
    s_c->fillRect(3, 76, 234, 35, PANEL);
    s_c->setClipRect(3, 76, 234, 35);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_center);
    for (int p = (int)floorf(vd) - 21; p <= (int)ceilf(vd) + 21; ++p) {
        const int x = 120 + (int)lroundf((p - vd) * 6);
        if (p < 0 || p > 100) {
            s_c->drawLine(x, 110, x + 6, 76, LINE);  // beyond the hard stops
            continue;
        }
        if (p % 10 == 0) {
            s_c->fillRect(x - 1, 76, 2, 15, TEXT);
            if (p == 0) {  // near: a person
                s_c->fillCircle(x + 9, 97, 3, TEXT_2);
                s_c->fillRoundRect(x + 5, 101, 8, 6, 2, TEXT_2);
            } else if (p == 100) {  // far: mountains
                s_c->fillTriangle(x - 20, 106, x - 12, 97, x - 4, 106, TEXT_2);
                s_c->fillTriangle(x - 13, 106, x - 8, 101, x - 3, 106, TEXT_2);
            } else {
                char lbl[12];
                snprintf(lbl, sizeof(lbl), "%d", p);
                s_c->setTextColor(TEXT_2);
                s_c->drawString(lbl, x, 95);
            }
        } else if (p % 5 == 0) {
            s_c->fillRect(x, 76, 1, 10, TEXT_2);
        } else {
            s_c->fillRect(x, 76, 1, 5, LINE);
        }
        if (p == 0 || p == 100) s_c->fillRect(x - 1, 76, 3, 35, TEXT_2);
    }
    if (cv.actual.focusKnown) {  // where the camera actually is
        const int ax = 120 + (int)lroundf((cv.actual.focus * 100.0f - vd) * 6);
        if (abs(ax - 120) >= 2) markerUp(ax, 103, TEXT);
    }
    s_c->clearClipRect();
    s_c->fillTriangle(114, 70, 126, 70, 120, 78, ACCENT);
    s_c->fillRect(119, 76, 2, 35, ACCENT);
    // Minimap of the whole range.
    s_c->fillRect(6, 114, 228, 4, LINE);
    s_c->fillRect(6, 114, (int)(228 * vd / 100.0f), 4, TEXT_3);
    const int w0 = 6 + (int)(228 * fmaxf(0, vd - 20) / 100.0f);
    const int w1 = 6 + (int)(228 * fminf(100, vd + 20) / 100.0f);
    s_c->drawRect(w0, 113, w1 - w0, 6, TEXT_2);
    s_c->fillRect(6 + (int)(228 * vd / 100.0f) - 1, 113, 2, 6, ACCENT);

    if (!v.focusControllable) {
        drawHintMessage("LENS FOCUS NOT CONTROLLABLE");
        return;
    }
}

float irisX(float f) {
    if (f < 1) f = 1;
    return 10.0f + 2.0f * log2f(f) * 24.7f;
}

void drawIrisDetail(const UiState& s, float dt) {
    const CameraView& cv = s.cam;
    const CameraValues& v = cv.shown;
    detailTitle(s);
    int cx = stepChip(s, 234, "x3");
    const bool noLens = v.irisKnown && v.irisStop <= 0;
    if (noLens) cx = drawChip(cx, 18, "NO LENS", TEXT_3, false) - 4;
    if (v.irisAuto) cx = drawChip(cx, 18, "AUTO", INFO, false) - 4;
    if (!v.irisControllable) drawChip(cx, 18, "LOCKED", WARN, false);

    char val[12] = "--";
    if (v.irisKnown && v.irisStop > 0) irisText(v, val, sizeof(val));
    // Iris shows the confirmed stop; queued steps go in the chip.
    uint16_t heroColor = valueColor(s, RemoteRow::Iris);
    if (heroColor == ACCENT) heroColor = TEXT;
    const int hx = drawHero(s, "f/", val, nullptr, heroColor);
    if (cv.irisQueued != 0) {
        char t[16];
        snprintf(t, sizeof(t), "   %d", abs(cv.irisQueued));
        s_c->setFont(F0);
        const int w = s_c->textWidth(t) + 8;
        const int x0 = drawChip(hx + 8 + w, 42, t, ACCENT, false);
        drawArrow(x0 + 4, 45, cv.irisQueued > 0 ? Arrow::Right : Arrow::Left, ACCENT);
    }

    // Aperture: 7 blades open and close with the stop.
    const float target = (v.irisKnown && v.irisStop > 0) ? v.irisStop : 4.0f;
    A.irisDisp = ease(A.irisDisp, target, dt, 60, 0.01f);
    const int ax = 194, ay = 60;
    s_c->fillCircle(ax, ay, 28, BLADE);
    s_c->drawCircle(ax, ay, 28, LINE);
    s_c->drawCircle(ax, ay, 27, LINE);
    const float minF = v.irisMin > 0 ? v.irisMin : 1.4f;
    float r = 3.0f + 21.0f * (minF / A.irisDisp);
    if (r > 24) r = 24;
    const float rot = 0.9f * (1.0f - r / 24.0f);
    const uint16_t light = (v.irisAuto || noLens) ? TEXT_3 : LIGHT;
    const uint16_t seam = cv.irisQueued ? ACCENT : LINE;
    for (int k = 0; k < 7; ++k) {
        const float a0 = rot + k * 2.0f * (float)M_PI / 7.0f;
        const float a1 = rot + (k + 1) * 2.0f * (float)M_PI / 7.0f;
        s_c->fillTriangle(ax, ay, ax + (int)lroundf(r * cosf(a0)), ay + (int)lroundf(r * sinf(a0)),
                          ax + (int)lroundf(r * cosf(a1)), ay + (int)lroundf(r * sinf(a1)), light);
    }
    for (int k = 0; k < 7; ++k) {
        const float a0 = rot + k * 2.0f * (float)M_PI / 7.0f;
        s_c->drawLine(ax + (int)lroundf(r * cosf(a0)), ay + (int)lroundf(r * sinf(a0)),
                      ax + (int)lroundf(27 * cosf(a0 + 1.2f)), ay + (int)lroundf(27 * sinf(a0 + 1.2f)), seam);
    }

    // f-stop scale with the lens's range.
    static const float full[] = {1, 1.4f, 2, 2.8f, 4, 5.6f, 8, 11, 16, 22};
    static const char* const labels[] = {"1", "1.4", "2", "2.8", "4", "5.6", "8", "11", "16", "22"};
    s_c->drawFastHLine(10, 104, 220, LINE);
    for (int k = 0; k <= 26; ++k) {
        if (k % 3) s_c->drawFastVLine((int)(10 + (k / 3.0f) * 24.7f), 101, 3, LINE);
    }
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_2);
    for (int i = 0; i < 10; ++i) {
        const int x = (int)irisX(full[i]);
        s_c->drawFastVLine(x, 98, 6, TEXT_2);
        s_c->drawString(labels[i], x, 108);
    }
    if (v.irisMin > 0 && v.irisMax > v.irisMin) {
        const int x0 = (int)irisX(v.irisMin), x1 = (int)irisX(v.irisMax);
        s_c->fillRect(x0, 101, x1 - x0, 4, TEXT_2);
        for (int x = 10; x + 3 < x0; x += 4) s_c->drawLine(x, 104, x + 3, 101, LINE);
        for (int x = x1 + 2; x < 228; x += 4) s_c->drawLine(x, 104, x + 3, 101, LINE);
        const bool flash = within(s.nowMs, s.limitHitAt, 300);
        if (flash) A.active = true;
        s_c->drawFastVLine(x0, 98, 8, flash ? WARN : TEXT);
        s_c->drawFastVLine(x1, 98, 8, flash ? WARN : TEXT);
    }
    if (v.irisKnown && v.irisStop > 0) {
        const int mx = (int)irisX(A.irisDisp);
        markerDown(mx, 99, TEXT);
        const int n = abs(cv.irisQueued) > 3 ? 3 : abs(cv.irisQueued);
        for (int i = 0; i < n; ++i) {
            const int dx = cv.irisQueued > 0 ? 8 + i * 6 : -12 - i * 6;
            drawArrow(mx + dx, 92, cv.irisQueued > 0 ? Arrow::Right : Arrow::Left, ACCENT);
        }
    }

    if (v.irisAuto) {
        drawHintMessage("IRIS SET BY AUTO EXPOSURE");
        return;
    }
    if (!v.irisControllable || noLens) {
        drawHintMessage("LENS NOT CONTROLLABLE");
        return;
    }
}

void isoShort(int iso, char* out, size_t cap) {
    if (iso >= 1000) {
        const int t = iso / 100;
        if (t % 10 == 0) {
            snprintf(out, cap, "%dK", t / 10);
        } else {
            snprintf(out, cap, "%d.%dK", t / 10, t % 10);
        }
    } else {
        snprintf(out, cap, "%d", iso);
    }
}

void drawIsoDetail(const UiState& s, float dt) {
    const CameraValues& v = s.cam.shown;
    detailTitle(s);
    const int n = (int)v.isoCount;
    int idx = 0;
    for (int i = 1; i < n; ++i) {
        if (abs(v.isoList[i] - v.iso) < abs(v.isoList[idx] - v.iso)) idx = i;
    }
    const int cx = stepChip(s, 234, "1 STOP");
    if (s.isoIndexAtOpen >= 0 && n && idx != s.isoIndexAtOpen) {
        // Exposure change since the view opened, in thirds of a stop.
        const int d = idx - s.isoIndexAtOpen;
        const int whole = abs(d) / 3, frac = abs(d) % 3;
        const char sign = d > 0 ? '+' : '-';
        char t[20];
        if (whole && frac) {
            snprintf(t, sizeof(t), "%c%d %d/3 EV", sign, whole, frac);
        } else if (whole) {
            snprintf(t, sizeof(t), "%c%d EV", sign, whole);
        } else {
            snprintf(t, sizeof(t), "%c%d/3 EV", sign, frac);
        }
        drawChip(cx, 18, t, TEXT_2, false);
    }
    if (!n) {
        drawHero(s, nullptr, "--", nullptr, TEXT_3);
    } else {
        // Drum of values with the current one in a window.
        A.isoIdxDisp = ease(A.isoIdxDisp, (float)idx, dt, 45, 0.005f);
        const bool flash = within(s.nowMs, s.limitHitAt, 300);
        if (flash) A.active = true;
        s_c->fillRoundRect(64, 36, 112, 44, 5, PANEL_HI);
        s_c->setClipRect(3, 34, 234, 48);
        s_c->setTextDatum(textdatum_t::middle_center);
        for (int i = 0; i < n; ++i) {
            const float x = 120 + (i - A.isoIdxDisp) * 92;
            if (fabsf(x - 120) >= 160) continue;
            char t[10];
            snprintf(t, sizeof(t), "%d", v.isoList[i]);
            if (i == idx) {
                s_c->setFont(B18);
                s_c->setTextColor(valueColor(s, RemoteRow::Iso));
            } else {
                s_c->setFont(R9);
                s_c->setTextColor(blend(TEXT_2, TEXT_3, fminf(1, fabsf(x - 120) / 92.0f)));
            }
            s_c->drawString(t, (int)lroundf(x) + shakeOffset(s.nowMs, s.cam.failedAt), 58);
        }
        s_c->clearClipRect();
        s_c->drawRoundRect(64, 36, 112, 44, 5, flash ? WARN : ACCENT);
        s_c->drawRoundRect(65, 37, 110, 42, 5, flash ? WARN : ACCENT);
        // Ruler of all supported values.
        s_c->drawFastHLine(12, 100, 216, LINE);
        s_c->setFont(F0);
        for (int i = 0; i < n; ++i) {
            const int x = n > 1 ? 12 + i * 216 / (n - 1) : 120;
            const int iso = v.isoList[i];
            const bool fullStop = iso % 100 == 0 && ((iso / 100) & ((iso / 100) - 1)) == 0;
            s_c->drawFastVLine(x, fullStop ? 92 : 96, fullStop ? 8 : 4, fullStop ? TEXT_2 : LINE);
            if (iso == 100 || iso == 400 || iso == 1600 || iso == 6400 || iso == 25600) {
                char t[16];
                isoShort(iso, t, sizeof(t));
                s_c->setTextColor(TEXT_2);
                s_c->setTextDatum(x > 215 ? textdatum_t::top_right : textdatum_t::top_center);
                s_c->drawString(t, x > 215 ? 229 : x, 104);
            }
        }
        const int mx = n > 1 ? 12 + (int)(A.isoIdxDisp * 216 / (n - 1)) : 120;
        markerDown(mx, 99, ACCENT);
    }
}

struct Preset {
    int k;
    const char* name;
};
const Preset kPresets[] = {{3200, "TUNGSTEN"}, {4000, "FLUORESCENT"}, {5600, "DAYLIGHT"}, {6500, "CLOUDY"}, {7500, "SHADE"}};

const Preset* nearestPreset(int k) {
    for (const Preset& p : kPresets) {
        if (abs(p.k - k) <= 150) return &p;
    }
    return nullptr;
}

void drawPresetIcon(const Preset* p, int cx, int cy) {
    if (!p) {  // thermometer
        s_c->fillRoundRect(cx - 3, cy - 14, 6, 22, 3, TEXT_3);
        s_c->fillCircle(cx, cy + 9, 5, TEXT_3);
        return;
    }
    const uint16_t c = kelvinColor(p->k);
    switch (p->k) {
        case 3200:  // bulb
            s_c->fillCircle(cx, cy - 4, 10, c);
            s_c->fillRect(cx - 5, cy + 6, 10, 6, TEXT_2);
            s_c->drawFastHLine(cx - 5, cy + 8, 10, LINE);
            s_c->drawFastHLine(cx - 5, cy + 10, 10, LINE);
            break;
        case 4000:  // tube
            s_c->fillRoundRect(cx - 16, cy - 6, 32, 9, 4, c);
            s_c->fillRect(cx - 18, cy - 3, 2, 3, TEXT_2);
            s_c->fillRect(cx + 16, cy - 3, 2, 3, TEXT_2);
            break;
        case 5600:  // sun
            s_c->fillCircle(cx, cy, 8, c);
            for (int i = 0; i < 8; ++i) {
                const float a = i * (float)M_PI / 4;
                s_c->drawWideLine(cx + 11 * cosf(a), cy + 11 * sinf(a), cx + 15 * cosf(a), cy + 15 * sinf(a), 1, c);
            }
            break;
        case 6500:  // cloud
            s_c->fillCircle(cx - 9, cy + 3, 7, c);
            s_c->fillCircle(cx, cy - 2, 9, c);
            s_c->fillCircle(cx + 9, cy + 4, 6, c);
            s_c->fillRect(cx - 9, cy + 4, 18, 6, c);
            break;
        default:  // shade: a house
            s_c->fillTriangle(cx - 16, cy, cx + 16, cy, cx, cy - 14, c);
            s_c->fillRect(cx - 12, cy, 24, 12, c);
            break;
    }
}

void drawLutBar(const uint16_t* lut, int x, int y, int w, int h, float dim) {
    for (int i = 0; i < w; ++i) {
        const uint16_t c = lut[i * (LUT_W - 1) / (w - 1)];
        s_c->drawFastVLine(x + i, y, h, dim > 0 ? blend(c, BG, dim) : c);
    }
}

void drawWbDetail(const UiState& s, float dt) {
    const CameraView& cv = s.cam;
    const CameraValues& v = cv.shown;
    detailTitle(s);
    const Preset* preset = v.wbKnown ? nearestPreset(v.wb) : nullptr;
    int cx = stepChip(s, 234, "500K");
    if (cv.awb == OpState::Running) {
        if (blink(s.nowMs, 250)) cx = drawChip(cx, 18, "AUTO WB", INFO, true) - 4;
    } else if (cv.awb == OpState::Done) {
        cx = drawChip(cx, 18, "AUTO WB OK", OK, true) - 4;
    }
    char val[12] = "--";
    if (v.wbKnown) snprintf(val, sizeof(val), "%d", v.wb);
    drawHero(s, nullptr, val, v.wbKnown ? "K" : nullptr,
             cv.awb == OpState::Running ? TEXT_3 : valueColor(s, RemoteRow::Wb));
    drawPresetIcon(preset, 200, 42);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_2);
    s_c->drawString(preset ? preset->name : "CUSTOM", 200, 62);

    drawLutBar(kelvinLut, 10, 82, 220, 14, 0);
    for (const Preset& p : kPresets) s_c->drawFastVLine(10 + (p.k - 2500) * 219 / 7500, 97, 3, TEXT_3);
    s_c->setFont(F0);
    s_c->setTextColor(TEXT_3);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->drawString("2500", 10, 100);
    s_c->setTextDatum(textdatum_t::top_right);
    s_c->drawString("10000", 229, 100);
    if (cv.awb == OpState::Running) {  // scanning band
        const int sx = 10 + (int)(210 * ((s.nowMs - cv.awbAt) % 700) / 700.0f);
        s_c->drawFastVLine(sx, 82, 14, TEXT);
        s_c->drawFastVLine(sx + 10, 82, 14, TEXT);
        A.active = true;
    }
    if (v.wbKnown) {
        A.wbDisp = ease(A.wbDisp, (float)v.wb, dt, 50, 0.5f);
        const int mx = 10 + (int)((A.wbDisp - 2500) * 219.0f / 7500.0f);
        const bool flash = within(s.nowMs, s.limitHitAt, 300);
        if (flash) A.active = true;
        markerDown(mx, 81, flash ? WARN : (rowPending(cv, RemoteRow::Wb) ? ACCENT : TEXT));
        s_c->fillRect(mx - 1, 80, 3, 18, BG);
        s_c->drawFastVLine(mx, 80, 18, TEXT);
    }
    // Tint context.
    drawLutBar(tintLut, 22, 111, 196, 5, 0.5f);
    s_c->drawFastVLine(120, 110, 7, TEXT_3);
    if (v.tintKnown) s_c->fillRect(22 + (v.tint + 50) * 195 / 100, 110, 2, 7, TEXT);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->setTextColor(TEXT_3);
    s_c->drawString("G", 12, 110);
    s_c->drawString("M", 224, 110);

}

void drawTintDetail(const UiState& s, float dt) {
    const CameraValues& v = s.cam.shown;
    detailTitle(s);
    const int cx = stepChip(s, 234, "x10");
    if (v.tintKnown) {
        if (v.tint < 0) {
            drawChip(cx, 18, "GREEN", TINT_G, false);
        } else if (v.tint > 0) {
            drawChip(cx, 18, "MAGENTA", TINT_M, false);
        } else {
            drawChip(cx, 18, "NEUTRAL", TEXT_2, false);
        }
    }
    char val[8] = "--";
    if (v.tintKnown) {
        if (v.tint == 0) {
            snprintf(val, sizeof(val), "0");
        } else {
            snprintf(val, sizeof(val), "%+d", v.tint);
        }
    }
    drawHero(s, nullptr, val, nullptr, valueColor(s, RemoteRow::Tint));
    // A grey card as the tint would shift it.
    const float t = v.tintKnown ? v.tint / 50.0f : 0;
    int r = 128, g = 128, b = 128;
    if (t > 0) {
        r += (int)(60 * t);
        g -= (int)(60 * t);
        b += (int)(50 * t);
    } else {
        g += (int)(60 * -t);
        r -= (int)(40 * -t);
        b -= (int)(40 * -t);
    }
    s_c->fillRoundRect(164, 30, 66, 36, 4, rgb((uint8_t)r, (uint8_t)g, (uint8_t)b));
    s_c->drawRoundRect(164, 30, 66, 36, 4, LINE);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_3);
    s_c->drawString("GREY CARD", 197, 68);

    drawLutBar(tintLut, 10, 82, 220, 16, 0);
    // Centre detent clicks green when the value lands on or crosses 0.
    const int side = !v.tintKnown ? A.tintSide : (v.tint > 0 ? 1 : (v.tint < 0 ? -1 : 0));
    if (side != A.tintSide) {
        if (side == 0 || A.tintSide != 0) A.detentAt = s.nowMs;
        A.tintSide = side;
    }
    const float dflash = flashT(s.nowMs, A.detentAt, 250);
    s_c->fillRect(119, 78, 2, 24, dflash < 1 ? OK : TEXT);
    s_c->setTextColor(TEXT_2);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->drawString("-50 G", 10, 103);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->drawString("0", 120, 103);
    s_c->setTextDatum(textdatum_t::top_right);
    s_c->drawString("M +50", 229, 103);
    if (v.tintKnown) {
        A.tintDisp = ease(A.tintDisp, (float)v.tint, dt, 50, 0.05f);
        const int mx = 10 + (int)((A.tintDisp + 50) * 219.0f / 100.0f);
        const bool flash = within(s.nowMs, s.limitHitAt, 300);
        if (flash) A.active = true;
        markerDown(mx, 81, flash ? WARN : (rowPending(s.cam, RemoteRow::Tint) ? ACCENT : TEXT));
        s_c->fillRect(mx - 1, 80, 3, 20, BG);
        s_c->drawFastVLine(mx, 80, 20, TEXT);
    }
    drawLutBar(kelvinLut, 10, 114, 220, 4, 0.5f);
    if (v.wbKnown) s_c->fillRect(10 + (v.wb - 2500) * 219 / 7500, 113, 2, 6, TEXT);

}

// A tiny picture of you on a monitor, with OSD bars when clean feed is off.
void drawMiniMonitor(int x, int y, int w, int h, float overlay) {
    s_c->setClipRect(x, y, w, h);
    s_c->fillRect(x, y, w, h * 6 / 10, SKY);
    s_c->fillRect(x, y + h * 6 / 10, w, h - h * 6 / 10, GROUND);
    s_c->fillCircle(x + w - 10, y + 7, 4, SUN);
    const int px = x + w / 2;
    s_c->fillRoundRect(px - 6, y + h - 12, 12, 12, 3, TEXT_3);
    s_c->fillCircle(px, y + h - 16, 4, SKIN);
    if (overlay > 0.01f) {
        const int topY = y + 3 - (int)((1 - overlay) * 8);
        const int botY = y + h - 4 + (int)((1 - overlay) * 8);
        for (int i = 0; i < 3; ++i) s_c->fillRect(x + 4 + i * 11, topY, 8, 2, TEXT);
        s_c->fillRect(x + 4, botY, 10, 2, TEXT);
        s_c->fillRect(x + 17, botY, 10, 2, TEXT);
        s_c->fillRect(x + w - 8, botY, 4, 2, ERR);
        drawAfFrame(x + w / 2, y + h / 2, h / 2 - 4, 4, 1, blend(BG, TEXT_2, overlay));
    }
    s_c->clearClipRect();
}

void drawFeedDetail(const UiState& s, float dt) {
    const CameraValues& v = s.cam.shown;
    detailTitle(s);
    const int n = (int)v.displayCount;
    if (!n) {
        drawHero(s, nullptr, "--", nullptr, TEXT_3);
        return;
    }
    const int tw = n <= 2 ? 110 : (n == 3 ? 74 : 56);
    const int total = n * tw + (n - 1) * 6;
    const int x0 = (W - total) / 2;
    A.feedSelX = ease(A.feedSelX, (float)(x0 + s.feedDisplay * (tw + 6)), dt, 30, 0.25f);
    for (int i = 0; i < n; ++i) {
        const int tx = x0 + i * (tw + 6), ty = 32;
        s_c->fillRoundRect(tx, ty, tw, 86, 4, PANEL);
        s_c->drawRoundRect(tx, ty, tw, 86, 4, LINE);
        const bool known = v.cleanFeedKnown[i];
        const bool on = known && v.cleanFeed[i];
        const bool pending = v.cleanFeed[i] != s.cam.actual.cleanFeed[i];
        A.overlay[i] = ease(A.overlay[i], on ? 0.0f : 1.0f, dt, 40, 0.01f);
        const int mw = tw - 12;
        drawMiniMonitor(tx + 6, ty + 6, mw, mw * 9 / 16, A.overlay[i]);
        char name[8];
        shortDisplayName(v.displays[i], name, sizeof(name));
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::middle_center);
        s_c->setTextColor(TEXT);
        s_c->drawString(name, tx + tw / 2, ty + 50);
        const int cy = ty + 66;
        s_c->setFont(F0);
        if (!known) {
            s_c->setTextColor(TEXT_3);
            s_c->drawString("--", tx + tw / 2, cy + 7);
        } else if (pending) {
            s_c->fillRoundRect(tx + 6, cy, tw - 12, 14, 3, ACCENT);
            s_c->setTextColor(BG);
            s_c->drawString(on ? "CLEAN" : "OVERLAY", tx + tw / 2, cy + 7);
        } else if (on) {
            s_c->fillRoundRect(tx + 6, cy, tw - 12, 14, 3, OK);
            s_c->setTextColor(BG);
            s_c->drawString("CLEAN", tx + tw / 2, cy + 7);
        } else {
            s_c->drawRoundRect(tx + 6, cy, tw - 12, 14, 3, LINE);
            s_c->setTextColor(TEXT_2);
            s_c->drawString("OVERLAY", tx + tw / 2, cy + 7);
        }
    }
    const int sx = (int)lroundf(A.feedSelX);
    s_c->drawRoundRect(sx, 32, tw, 86, 4, ACCENT);
    s_c->drawRoundRect(sx + 1, 33, tw - 2, 84, 4, ACCENT);
}

// --- connection, help, intro, overlays ---------------------------------------
void drawStepNode(int x, int y, int state, uint32_t now) {  // 0 future, 1 current, 2 done
    if (state == 2) {
        s_c->fillCircle(x, y, 8, OK);
        s_c->drawWideLine(x - 4, y, x - 1, y + 3, 1, BG);
        s_c->drawWideLine(x - 1, y + 3, x + 4, y - 3, 1, BG);
    } else if (state == 1) {
        const float br = 10 + 1.5f * sinf(now * 2 * (float)M_PI / 1000.0f);
        s_c->drawCircle(x, y, (int)lroundf(br), ACCENT_DIM);
        s_c->drawCircle(x, y, 8, ACCENT);
        s_c->drawCircle(x, y, 7, ACCENT);
        s_c->fillCircle(x, y, 3, ACCENT);
        A.active = true;
    } else {
        s_c->drawCircle(x, y, 8, TEXT_3);
    }
}

void drawConnection(const UiState& s) {
    const int step = s.cam.phase == CameraPhase::NoUsb ? 0 : (s.cam.phase == CameraPhase::WaitingIp ? 1 : 2);
    const int xs[3] = {40, 120, 200};
    const char* names[3] = {"USB", "NETWORK", "CAMERA"};
    for (int i = 0; i < 2; ++i) s_c->drawWideLine(xs[i] + 11, 25, xs[i + 1] - 11, 25, 1, i < step ? OK : LINE);
    for (int i = 0; i < 3; ++i) {
        drawStepNode(xs[i], 25, i < step ? 2 : (i == step ? 1 : 0), s.nowMs);
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(TEXT_2);
        s_c->drawString(names[i], xs[i], 37);
    }
    const char* title = "PLUG INTO THE CAMERA";
    const char* sub = "USB cable into the camera";
    char dots[24];
    if (s.cam.phase == CameraPhase::WaitingIp) {
        title = "WAITING FOR CAMERA";
        sub = "Set camera network to DHCP";
    } else if (s.cam.phase == CameraPhase::Connecting) {
        const int d = 1 + (int)((s.nowMs / 400) % 3);
        snprintf(dots, sizeof(dots), "CONNECTING%.*s", d, "...");
        title = dots;
        sub = "Talking to the camera";
        A.active = true;
    }
    s_c->setFont(B9);
    s_c->setTextDatum(textdatum_t::baseline_center);
    s_c->setTextColor(ACCENT);
    s_c->drawString(title, W / 2, 61);

    if (s.powerSwitchHint) {
        // No USB host after 15 s. The board cannot tell why: the Cardputer's
        // battery switch on (the camera then never starts its USB host), a
        // charge-only cable or a loose plug all look the same. A checklist,
        // not an accusation.
        s_c->fillRoundRect(4, 66, 232, 54, 5, PANEL);
        s_c->drawRoundRect(4, 66, 232, 54, 5, WARN);
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(WARN);
        s_c->drawString("No USB data from camera", W / 2, 69);
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        s_c->drawString("- CARDPUTER's own switch: OFF", 14, 88);
        s_c->drawString("- Data cable, firmly plugged in", 14, 99);
        s_c->setTextColor(TEXT_2);
        s_c->drawString("- then unplug and replug it", 14, 110);
    } else {
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(TEXT);
        s_c->drawString(sub, W / 2, 65);
        s_c->fillRoundRect(4, 84, 232, 36, 4, PANEL);
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        const char* labels[3] = {"WIFI", "PASS", "WEB"};
        const char* vals[3] = {s.ssid, s.password, "http://192.168.73.1"};
        for (int i = 0; i < 3; ++i) {
            s_c->setTextColor(TEXT_2);
            s_c->drawString(labels[i], 10, 88 + i * 11);
            s_c->setTextColor(i == 2 ? INFO : TEXT);
            s_c->drawString(vals[i], 50, 88 + i * 11);
        }
    }
}

// Help: two pages in the large font. Page 1 = keys, page 2 = Wi-Fi.
void helpHeader(const char* title, const char* right) {
    s_c->fillRect(0, 0, W, STATUS_H, STATUS_BG);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::middle_left);
    s_c->setTextColor(ACCENT);
    s_c->drawString(title, 4, 9);
    s_c->setTextDatum(textdatum_t::middle_right);
    s_c->setTextColor(TEXT_2);
    s_c->drawString(right, W - 4, 9);
}

void helpLine(int y, const char* key, const char* text, uint16_t textColor = TEXT, int textX = 66) {
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_left);
    if (key) {
        s_c->setTextColor(ACCENT);
        s_c->drawString(key, 6, y);
    }
    s_c->setTextColor(textColor);
    s_c->drawString(text, textX, y);
}

void helpArrows(int x, int y, const Arrow* a, int n) {
    for (int i = 0; i < n; ++i) drawArrow(x + i * 9, y + 4, a[i], ACCENT);
}

void drawHelp(const UiState& s) {
    if (s.helpPage == 0) {
        helpHeader("KEYS", "h: Wi-Fi page");
        static const Arrow all[] = {Arrow::Up, Arrow::Down, Arrow::Left, Arrow::Right};
        static const Arrow lr[] = {Arrow::Left, Arrow::Right};
        static const Arrow ud[] = {Arrow::Up, Arrow::Down};
        int y = 21;
        helpArrows(6, y, all, 4);
        helpLine(y, nullptr, "pick a control");
        y += 15;
        helpLine(y, "Enter", "open it");
        y += 15;
        helpArrows(6, y, lr, 2);
        helpLine(y, nullptr, "open: change");
        y += 15;
        helpArrows(6, y, ud, 2);
        helpLine(y, nullptr, "open: big step");
        y += 15;
        helpLine(y, "Enter", "open: AF / auto WB / feed");
        y += 15;
        helpLine(y, "Space", "AUTOFOCUS  (or G0)", INFO);
        y += 15;
        helpLine(y, "Del", "back to all controls");
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(TEXT_2);
        s_c->drawString("other keys close this", W / 2, 120);
        return;
    }
    helpHeader("WI-FI", "h: keys page");
    char line[96];
    helpLine(22, "Wi-Fi", s.ssid, TEXT, 76);
    helpLine(39, "Pass", s.password, TEXT, 76);
    helpLine(56, "Setup", "http://192.168.73.1", INFO, 76);
    snprintf(line, sizeof(line), "%d connected", s.wifiClients);
    helpLine(73, "Phones", line, TEXT, 76);
    helpLine(90, "f i o w t c", "jump", TEXT, 104);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_2);
    s_c->drawString("Enter: replay intro", W / 2, 116);
}

void bigCap(int x, int y, int w, int h, const char* legend, uint16_t fill) {
    s_c->fillRoundRect(x, y, w, h, 5, fill);
    s_c->drawRoundRect(x, y, w, h, 5, LINE);
    s_c->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, LINE);
    s_c->drawFastHLine(x + 3, y + h, w - 6, LINE);
    s_c->drawFastHLine(x + 3, y + h + 1, w - 6, LINE);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::middle_center);
    s_c->setTextColor(fill == PANEL_HI ? TEXT : BG);
    s_c->drawString(legend, x + w / 2, y + h / 2);
}

void drawIntro(const UiState& s) {
    s_c->fillRect(0, 0, W, 14, STATUS_BG);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_left);
    s_c->setTextColor(ACCENT);
    s_c->drawString("WELCOME", 4, 3);
    s_c->setTextDatum(textdatum_t::top_right);
    s_c->setTextColor(TEXT_2);
    s_c->drawString(s.introCard == 0 ? "1 / 2" : "2 / 2", 236, 3);
    if (s.introCard == 0) {
        s_c->setFont(B9);
        s_c->setTextDatum(textdatum_t::baseline_center);
        s_c->setTextColor(TEXT);
        s_c->drawString("ONE-PRESS AUTOFOCUS", W / 2, 34);
        const OpState af = s.cam.af;
        const uint16_t fill = af == OpState::Running ? INFO : (af == OpState::Done ? OK : PANEL_HI);
        bigCap(20, 44, 110, 26, af == OpState::Done ? "SPACE  OK" : "SPACE", fill);
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT_2);
        s_c->drawString("or", 140, 50);
        bigCap(166, 44, 46, 26, "G0", PANEL_HI);
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(TEXT_2);
        s_c->drawString("side button", 189, 74);
        s_c->setFont(F2);
        s_c->drawString("focuses on the picture centre", W / 2, 82);
        s_c->setFont(F0);
        if (blink(s.nowMs, 600)) {
            s_c->setTextColor(ACCENT);
            s_c->drawString("try it now: press SPACE", W / 2, 100);
        }
    } else {
        struct Cap {
            int x, y;
            const char* legend;
            Arrow a;
        };
        const Cap caps[] = {{44, 22, ";", Arrow::Up}, {10, 48, ",", Arrow::Left}, {44, 48, ".", Arrow::Down}, {78, 48, "/", Arrow::Right}};
        for (const Cap& c : caps) {
            s_c->fillRoundRect(c.x, c.y, 30, 22, 4, PANEL_HI);
            s_c->drawRoundRect(c.x, c.y, 30, 22, 4, LINE);
            const int mx = c.x + 13, my = c.y + 10;
            switch (c.a) {
                case Arrow::Up: s_c->fillTriangle(mx - 5, my + 3, mx + 5, my + 3, mx, my - 5, ACCENT); break;
                case Arrow::Down: s_c->fillTriangle(mx - 5, my - 3, mx + 5, my - 3, mx, my + 5, ACCENT); break;
                case Arrow::Left: s_c->fillTriangle(mx + 3, my - 5, mx + 3, my + 5, mx - 5, my, ACCENT); break;
                default: s_c->fillTriangle(mx - 3, my - 5, mx - 3, my + 5, mx + 5, my, ACCENT); break;
            }
            s_c->setFont(F0);
            s_c->setTextDatum(textdatum_t::top_left);
            s_c->setTextColor(TEXT_2);
            s_c->drawString(c.legend, c.x + 22, c.y + 12);
        }
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        drawArrow(118, 25, Arrow::Up, ACCENT);
        drawArrow(125, 25, Arrow::Down, ACCENT);
        drawArrow(132, 25, Arrow::Left, ACCENT);
        drawArrow(139, 25, Arrow::Right, ACCENT);
        s_c->drawString("pick a control", 148, 24);
        int x = 118 + drawKeycap(118, 37, "ENT", Arrow::None, CapStyle::Normal) + 3;
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        s_c->drawString("open it", x, 39);
        drawArrow(118, 54, Arrow::Left, ACCENT);
        drawArrow(125, 54, Arrow::Right, ACCENT);
        s_c->drawString("change", 134, 53);
        drawArrow(176, 54, Arrow::Up, ACCENT);
        drawArrow(183, 54, Arrow::Down, ACCENT);
        s_c->drawString("big", 192, 53);
        x = 118 + drawKeycap(118, 66, "DEL", Arrow::None, CapStyle::Normal) + 3;
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        s_c->drawString("back to all", x, 68);
        x = 118 + drawKeycap(118, 82, "f", Arrow::None, CapStyle::Normal) + 2;
        x += drawKeycap(x, 82, "i", Arrow::None, CapStyle::Normal) + 2;
        x += drawKeycap(x, 82, "o", Arrow::None, CapStyle::Normal) + 3;
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        s_c->drawString("jump to", x, 84);
        x = 118 + drawKeycap(118, 98, "h", Arrow::None, CapStyle::Normal) + 3;
        s_c->setFont(F0);
        s_c->setTextDatum(textdatum_t::top_left);
        s_c->setTextColor(TEXT);
        s_c->drawString("all keys, any time", x, 100);
    }
    s_c->fillCircle(116, 114, 2, s.introCard == 0 ? ACCENT : LINE);
    s_c->fillCircle(124, 114, 2, s.introCard == 1 ? ACCENT : LINE);
    s_c->setFont(F2);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_2);
    s_c->drawString(s.introCard == 0 ? "any other key: next" : "any key: start", W / 2, 119);
}

void drawResetOverlay(uint32_t heldMs) {
    if (heldMs < 1000) return;
    A.active = true;
    s_c->fillScreen(BG);
    s_c->drawRect(0, 0, W, H, ERR);
    s_c->drawRect(1, 1, W - 2, H - 2, ERR);
    if (heldMs >= FACTORY_RESET_HOLD_MS) {
        s_c->fillScreen(ERR);
        s_c->setFont(B12);
        s_c->setTextDatum(textdatum_t::middle_center);
        s_c->setTextColor(TEXT);
        s_c->drawString("RESETTING", W / 2, H / 2);
        return;
    }
    s_c->setFont(B9);
    s_c->setTextDatum(textdatum_t::baseline_center);
    s_c->setTextColor(ERR);
    s_c->drawString("FACTORY RESET", W / 2, 20);
    const float p = (heldMs - 1000) / (float)(FACTORY_RESET_HOLD_MS - 1000);
    s_c->fillArc(120, 62, 30, 36, 0, 360, LINE);
    const float end = 270 + 360 * p;
    if (end <= 360) {
        s_c->fillArc(120, 62, 30, 36, 270, end, ERR);
    } else {
        s_c->fillArc(120, 62, 30, 36, 270, 360, ERR);
        s_c->fillArc(120, 62, 30, 36, 0, end - 360, ERR);
    }
    char t[12];
    snprintf(t, sizeof(t), "%lu", (unsigned long)((FACTORY_RESET_HOLD_MS - heldMs + 999) / 1000));
    s_c->setFont(F7);
    s_c->setTextDatum(textdatum_t::middle_center);
    s_c->setTextColor(ERR);
    s_c->drawString(t, 120, 62);
    s_c->setFont(F0);
    s_c->setTextDatum(textdatum_t::top_center);
    s_c->setTextColor(TEXT_2);
    s_c->drawString("all settings erased", W / 2, 104);
    s_c->setFont(F2);
    s_c->setTextColor(TEXT);
    s_c->drawString("release G0 to cancel", W / 2, 115);
}

// Confirmations (pending -> settled) and changes made elsewhere.
void trackFeedback(const UiState& s) {
    const CameraView& cv = s.cam;
    for (int i = 0; i < REMOTE_ROWS; ++i) {
        const bool p = rowPending(cv, (RemoteRow)i);
        if (A.pendingPrev[i] && !p) A.confirmAt[i] = s.nowMs;
        A.pendingPrev[i] = p;
    }
    const CameraValues& a = cv.actual;
    const CameraValues& o = A.actualPrev;
    if (A.havePrev && a.connected && o.connected) {
        const bool recentKey = within(s.nowMs, s.pressedAt, 1500);
        auto ext = [&](RemoteRow r, bool changed) {
            if (changed && !rowPending(cv, r) && !recentKey) A.externalAt[(int)r] = s.nowMs;
        };
        ext(RemoteRow::Focus, o.focusKnown && fabsf(a.focus - o.focus) > 0.02f && cv.af == OpState::Idle);
        ext(RemoteRow::Iris, o.irisKnown && fabsf(a.irisStop - o.irisStop) > 0.05f);
        ext(RemoteRow::Iso, o.isoKnown && a.iso != o.iso);
        ext(RemoteRow::Wb, o.wbKnown && a.wb != o.wb && cv.awb == OpState::Idle);
        ext(RemoteRow::Tint, o.tintKnown && a.tint != o.tint);
        bool feed = false;
        for (size_t i = 0; i < a.displayCount && i < o.displayCount; ++i) {
            feed |= o.cleanFeedKnown[i] && a.cleanFeed[i] != o.cleanFeed[i];
        }
        ext(RemoteRow::Feed, feed);
    }
    A.actualPrev = a;
    A.havePrev = true;
}

void drawScene(const UiState& s, float dt) {
    s_c->fillScreen(BG);
    if (s.view == UiView::Help) {
        drawHelp(s);
    } else if (s.view == UiView::Intro && s.cam.phase == CameraPhase::Connected) {
        drawIntro(s);
    } else if (s.cam.phase != CameraPhase::Connected) {
        drawStatusBar(s, false);
        s_c->setShift(CONTENT_SHIFT);
        drawConnection(s);
        s_c->setShift(0);
        drawBanner(s);
    } else {
        drawStatusBar(s, true);
        s_c->setShift(CONTENT_SHIFT);
        if (s.view == UiView::Detail) {
            switch (s.row) {
                case RemoteRow::Focus: drawFocusDetail(s, dt); break;
                case RemoteRow::Iris: drawIrisDetail(s, dt); break;
                case RemoteRow::Iso: drawIsoDetail(s, dt); break;
                case RemoteRow::Wb: drawWbDetail(s, dt); break;
                case RemoteRow::Tint: drawTintDetail(s, dt); break;
                default: drawFeedDetail(s, dt); break;
            }
        } else {
            drawOverview(s, dt);
        }
        s_c->setShift(0);
        drawBanner(s);
        drawTallyBorder(s);
    }
    drawResetOverlay(s.resetHeldMs);
}
}  // namespace

void uiInit() {
    s_lcd.init();
    s_lcd.setRotation(1);
    s_lcd.setBrightness(255);
    s_lcd.fillScreen(TFT_BLACK);
    initLuts();
    s_sprite = new LGFX_Sprite(&s_lcd);
    s_sprite->setColorDepth(16);
    s_sprite->setPsram(false);
    if (!s_sprite->createSprite(W, BAND_H)) {
        delete s_sprite;
        s_sprite = nullptr;
        return;
    }
    s_c = new Canvas(s_sprite);
}

bool uiRender(const UiState& s) {
    if (!s_c) return false;
    float dt = A.lastMs ? (float)(s.nowMs - A.lastMs) : 33.0f;
    if (dt > 100) dt = 100;
    A.lastMs = s.nowMs;
    trackFeedback(s);
    bool active = false;
    // The scene is drawn once per band; values ease only in the first band
    // (the second band gets dt = 0 so both halves match).
    for (int band = 0; band < H; band += BAND_H) {
        A.active = false;
        s_c->setBand(band);
        drawScene(s, band == 0 ? dt : 0.0f);
        active |= A.active;
        s_sprite->pushSprite(0, band);
    }
    return active;
}

void uiRenderSafeMode(uint32_t secondsLeft, uint32_t totalSeconds, uint32_t resetHeldMs) {
    if (!s_c) return;
    for (int band = 0; band < H; band += BAND_H) {
        s_c->setBand(band);
        s_c->fillScreen(BG);
        s_c->fillRect(0, 0, W, 16, ACCENT);
        for (int x = -16; x < W; x += 16) s_c->fillTriangle(x, 16, x + 8, 16, x + 16, 0, BG);
        s_c->setFont(B12);
        s_c->setTextDatum(textdatum_t::baseline_center);
        s_c->setTextColor(ACCENT);
        s_c->drawString("SAFE MODE", W / 2, 44);
        s_c->setFont(F2);
        s_c->setTextDatum(textdatum_t::top_center);
        s_c->setTextColor(TEXT);
        s_c->drawString("USB is a serial port for flashing.", W / 2, 54);
        s_c->drawString("Network and remote are off.", W / 2, 72);
        s_c->fillRoundRect(20, 96, 200, 6, 3, PANEL);
        const int w = totalSeconds ? (int)(200 * secondsLeft / totalSeconds) : 0;
        if (w > 0) s_c->fillRoundRect(20, 96, w, 6, 3, ACCENT);
        char t[32];
        snprintf(t, sizeof(t), "normal mode in %lu:%02lu", (unsigned long)(secondsLeft / 60),
                 (unsigned long)(secondsLeft % 60));
        s_c->setTextColor(TEXT_2);
        s_c->drawString(t, W / 2, 108);
        drawResetOverlay(resetHeldMs);
        s_sprite->pushSprite(0, band);
    }
}
