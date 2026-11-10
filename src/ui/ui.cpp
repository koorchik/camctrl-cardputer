#include "ui/ui.h"

#include <cstdio>
#include <cstring>

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>

#include "button.h"

namespace {
// Cardputer ADV display: ST7789V2 135 x 240 on SPI3, used in landscape.
// Explicit config (no board autodetect: it would probe the I2C bus the
// keyboard owns).
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

CardputerDisplay s_lcd;
LGFX_Sprite* s_fb = nullptr;
constexpr int W = 240;
constexpr int H = 135;
constexpr int HEADER_H = 16;
constexpr int ROW_H = 17;
constexpr int SELECTED_H = 34;

// Colours (RGB565).
const uint16_t C_BG = lgfx::color565(0, 0, 0);
const uint16_t C_HEADER = lgfx::color565(28, 28, 32);
const uint16_t C_ROW = lgfx::color565(14, 14, 16);
const uint16_t C_ROW_ALT = lgfx::color565(22, 22, 26);
const uint16_t C_SEL = lgfx::color565(22, 96, 210);
const uint16_t C_SEL_DARK = lgfx::color565(12, 56, 130);
const uint16_t C_TEXT = lgfx::color565(240, 240, 240);
const uint16_t C_LABEL = lgfx::color565(150, 150, 160);
const uint16_t C_DIM = lgfx::color565(90, 90, 96);
const uint16_t C_ON = lgfx::color565(40, 200, 90);
const uint16_t C_WARN = lgfx::color565(255, 176, 0);
const uint16_t C_RED = lgfx::color565(220, 40, 40);
const uint16_t C_HINT = lgfx::color565(190, 210, 255);

const char* const kLabels[REMOTE_ROWS] = {"FOCUS", "IRIS", "ISO", "WB", "TINT", "FEED"};

bool rowEnabled(const CameraValues& v, RemoteRow r) {
    switch (r) {
        case RemoteRow::Focus: return v.focusKnown && v.focusControllable;
        case RemoteRow::Iris: return v.irisKnown && v.irisControllable && !v.irisAuto;
        case RemoteRow::Iso: return v.isoKnown && v.isoCount > 0;
        case RemoteRow::Wb: return v.wbKnown;
        case RemoteRow::Tint: return v.tintKnown;
        case RemoteRow::Feed: return v.displayCount > 0;
        default: return false;
    }
}

void valueText(const CameraValues& v, RemoteRow r, char* out, size_t cap) {
    switch (r) {
        case RemoteRow::Focus: formatFocus(v, out, cap); break;
        case RemoteRow::Iris: formatIris(v, out, cap); break;
        case RemoteRow::Iso: formatIso(v, out, cap); break;
        case RemoteRow::Wb: formatWb(v, out, cap); break;
        case RemoteRow::Tint: formatTint(v, out, cap); break;
        default: out[0] = '\0'; break;
    }
}

const char* hintFor(const CameraValues& v, RemoteRow r) {
    switch (r) {
        case RemoteRow::Focus: return v.focusControllable ? "< >  ENTER/SPACE: AF" : "lens not controllable";
        case RemoteRow::Iris:
            if (v.irisAuto) return "auto exposure has it";
            return v.irisControllable ? "< > close/open" : "lens not controllable";
        case RemoteRow::Iso: return "< >";
        case RemoteRow::Wb: return "< >  ENTER: auto WB";
        case RemoteRow::Tint: return "< >";
        case RemoteRow::Feed: return "< > pick  ENTER: on/off";
        default: return "";
    }
}

// Clean feed outputs as pills: green = clean feed on.
void drawFeed(const UiState& s, int x, int y, int h, bool selected, bool big) {
    const CameraValues& v = s.cam;
    if (v.displayCount == 0) {
        s_fb->setTextColor(C_DIM);
        s_fb->setTextDatum(textdatum_t::middle_right);
        s_fb->drawString("--", W - 6, y + h / 2);
        return;
    }
    if (big) {
        s_fb->setFont(&fonts::FreeSansBold9pt7b);
    } else {
        s_fb->setFont(&fonts::Font2);
    }
    s_fb->setTextDatum(textdatum_t::middle_center);
    const int pillH = big ? 20 : 13;
    const int gap = 4;
    int widths[CAM_MAX_DISPLAYS] = {};
    char names[CAM_MAX_DISPLAYS][8] = {};
    int total = 0;
    for (size_t i = 0; i < v.displayCount; ++i) {
        shortDisplayName(v.displays[i], names[i], sizeof(names[i]));
        widths[i] = s_fb->textWidth(names[i]) + (big ? 14 : 10);
        total += widths[i] + (i ? gap : 0);
    }
    int px = W - 6 - total;
    if (px < x) px = x;
    const int py = y + (h - pillH) / 2 + (big ? 2 : 0);
    for (size_t i = 0; i < v.displayCount; ++i) {
        const bool on = v.cleanFeedKnown[i] && v.cleanFeed[i];
        const uint16_t fill = on ? C_ON : (selected ? C_SEL_DARK : C_ROW_ALT);
        s_fb->fillRoundRect(px, py, widths[i], pillH, 4, fill);
        if (selected && (int)i == s.feedDisplay) {
            s_fb->drawRoundRect(px - 1, py - 1, widths[i] + 2, pillH + 2, 5, C_TEXT);
            s_fb->drawRoundRect(px - 2, py - 2, widths[i] + 4, pillH + 4, 6, C_TEXT);
        }
        s_fb->setTextColor(on ? C_BG : (v.cleanFeedKnown[i] ? C_TEXT : C_DIM));
        s_fb->drawString(names[i], px + widths[i] / 2, py + pillH / 2 + 1);
        px += widths[i] + gap;
    }
}

void drawRow(const UiState& s, RemoteRow r, int y, bool selected) {
    const CameraValues& v = s.cam;
    const int h = selected ? SELECTED_H : ROW_H;
    const bool enabled = rowEnabled(v, r);
    s_fb->fillRect(0, y, W, h, selected ? C_SEL : (((int)r % 2) ? C_ROW_ALT : C_ROW));

    if (!selected) {
        s_fb->setFont(&fonts::Font2);
        s_fb->setTextDatum(textdatum_t::middle_left);
        s_fb->setTextColor(C_LABEL);
        s_fb->drawString(kLabels[(int)r], 8, y + h / 2 + 1);
        if (r == RemoteRow::Feed) {
            drawFeed(s, 70, y, h, false, false);
            return;
        }
        char val[24];
        valueText(v, r, val, sizeof(val));
        s_fb->setFont(&fonts::FreeSansBold9pt7b);
        s_fb->setTextDatum(textdatum_t::middle_right);
        s_fb->setTextColor(enabled ? C_TEXT : C_DIM);
        s_fb->drawString(val, W - 8, y + h / 2 + 1);
        return;
    }

    // Selected row: label + hint on the left, big value on the right.
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextDatum(textdatum_t::top_left);
    s_fb->setTextColor(C_TEXT);
    s_fb->drawString(kLabels[(int)r], 8, y + 2);
    s_fb->setFont(&fonts::Font0);
    s_fb->setTextColor(C_HINT);
    s_fb->drawString(hintFor(v, r), 8, y + 21);

    if (r == RemoteRow::Feed) {
        drawFeed(s, 90, y, h, true, true);
        return;
    }
    char val[24];
    valueText(v, r, val, sizeof(val));
    s_fb->setFont(&fonts::FreeSansBold18pt7b);
    s_fb->setTextDatum(textdatum_t::middle_right);
    s_fb->setTextColor(enabled ? C_TEXT : C_LABEL);
    s_fb->drawString(val, W - 6, y + h / 2 + 1);

    // Position bars help when stepping by small amounts.
    if (r == RemoteRow::Focus && v.focusKnown) {
        s_fb->fillRect(0, y + h - 3, W, 3, C_SEL_DARK);
        s_fb->fillRect(0, y + h - 3, (int)(v.focus * W + 0.5f), 3, C_TEXT);
    } else if (r == RemoteRow::Iris && v.irisKnown) {
        const float n = v.irisNormalised < 0 ? 0 : (v.irisNormalised > 1 ? 1 : v.irisNormalised);
        s_fb->fillRect(0, y + h - 3, W, 3, C_SEL_DARK);
        s_fb->fillRect(0, y + h - 3, (int)(n * W + 0.5f), 3, C_TEXT);
    } else if (r == RemoteRow::Wb && v.wbKnown && v.wbMax > v.wbMin) {
        const float n = (float)(v.wb - v.wbMin) / (float)(v.wbMax - v.wbMin);
        s_fb->fillRect(0, y + h - 3, W, 3, C_SEL_DARK);
        s_fb->fillRect(0, y + h - 3, (int)(n * W + 0.5f), 3, C_TEXT);
    }
}

void drawHeader(const UiState& s) {
    if (s.toast[0]) {
        s_fb->fillRect(0, 0, W, HEADER_H, C_WARN);
        s_fb->setFont(&fonts::Font2);
        s_fb->setTextDatum(textdatum_t::middle_center);
        s_fb->setTextColor(C_BG);
        s_fb->drawString(s.toast, W / 2, HEADER_H / 2 + 1);
        return;
    }
    s_fb->fillRect(0, 0, W, HEADER_H, C_HEADER);
    s_fb->setFont(&fonts::Font0);
    s_fb->setTextDatum(textdatum_t::middle_left);
    // Event stream state: green = live, amber = polling.
    s_fb->fillCircle(6, HEADER_H / 2, 3, s.liveEvents ? C_ON : C_WARN);
    s_fb->setTextColor(C_TEXT);
    s_fb->drawString(s.cam.name[0] ? s.cam.name : "Camera", 14, HEADER_H / 2);
    char right[24];
    snprintf(right, sizeof(right), "WiFi %d  h:help", s.wifiClients);
    s_fb->setTextDatum(textdatum_t::middle_right);
    s_fb->setTextColor(C_LABEL);
    s_fb->drawString(right, W - 4, HEADER_H / 2);
}

void drawControls(const UiState& s) {
    drawHeader(s);
    int y = HEADER_H;
    for (int i = 0; i < REMOTE_ROWS; ++i) {
        const bool sel = (int)s.row == i;
        drawRow(s, (RemoteRow)i, y, sel);
        y += sel ? SELECTED_H : ROW_H;
    }
}

void drawLine(int y, const char* label, const char* value, uint16_t valueColor) {
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextDatum(textdatum_t::top_left);
    s_fb->setTextColor(C_LABEL);
    s_fb->drawString(label, 8, y);
    s_fb->setTextColor(valueColor);
    s_fb->drawString(value, 64, y);
}

void drawNoCamera(const UiState& s) {
    s_fb->fillRect(0, 0, W, HEADER_H, C_HEADER);
    s_fb->setFont(&fonts::Font0);
    s_fb->setTextDatum(textdatum_t::middle_left);
    s_fb->setTextColor(C_TEXT);
    s_fb->drawString("CamCtrl remote", 8, HEADER_H / 2);
    s_fb->setTextDatum(textdatum_t::middle_right);
    s_fb->setTextColor(C_LABEL);
    char right[24];
    snprintf(right, sizeof(right), "WiFi %d  h:help", s.wifiClients);
    s_fb->drawString(right, W - 4, HEADER_H / 2);

    const char* title = "CONNECT CAMERA";
    const char* sub = "Plug the USB cable into the camera";
    uint16_t color = C_WARN;
    if (s.phase == CameraPhase::WaitingIp) {
        title = "WAITING FOR CAMERA";
        sub = "Set the camera network to DHCP";
    } else if (s.phase == CameraPhase::Connecting) {
        title = "CONNECTING...";
        sub = "Talking to the camera";
        color = C_HINT;
    }
    s_fb->setFont(&fonts::FreeSansBold12pt7b);
    s_fb->setTextDatum(textdatum_t::top_center);
    s_fb->setTextColor(color);
    s_fb->drawString(title, W / 2, 22);
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextColor(C_TEXT);
    s_fb->drawString(sub, W / 2, 50);

    int y = 72;
    if (s.powerSwitchHint) {
        // Only the Cardputer's own switch matters: with its battery on, the
        // camera never starts its USB host.
        s_fb->fillRoundRect(4, y - 2, W - 8, 34, 4, C_RED);
        s_fb->setTextColor(C_TEXT);
        s_fb->drawString("In a camera? Set the", W / 2, y);
        s_fb->drawString("CARDPUTER switch OFF", W / 2, y + 15);
        y += 38;
        drawLine(y, "WiFi", s.ssid, C_TEXT);
        return;
    }
    drawLine(y, "WiFi", s.ssid, C_TEXT);
    drawLine(y + 16, "Pass", s.password, C_TEXT);
    drawLine(y + 32, "Setup", "http://192.168.73.1", C_HINT);
}

void drawHelp(const UiState& s) {
    s_fb->fillScreen(C_BG);
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextDatum(textdatum_t::top_left);
    struct Line {
        const char* key;
        const char* what;
    };
    const Line lines[] = {
        {"; .", "choose control (up/down)"},
        {", /", "change value, hold = fast"},
        {"Shift", "big steps"},
        {"Space G0", "AUTOFOCUS"},
        {"Enter", "AF / auto WB / feed on-off"},
        {"f i o w t c", "focus iris iso wb tint feed"},
    };
    int y = 2;
    for (const Line& l : lines) {
        s_fb->setTextColor(C_HINT);
        s_fb->drawString(l.key, 6, y);
        s_fb->setTextColor(C_TEXT);
        s_fb->drawString(l.what, 92, y);
        y += 16;
    }
    s_fb->drawFastHLine(6, y + 2, W - 12, C_DIM);
    s_fb->setTextColor(C_LABEL);
    char wifi[112];
    snprintf(wifi, sizeof(wifi), "%s / %s", s.ssid, s.password);
    s_fb->drawString(wifi, 6, y + 6);
    s_fb->drawString("Phone app: http://192.168.73.2 camera", 6, y + 22);
}

void drawResetBar(uint32_t heldMs) {
    if (heldMs < 500) return;
    const uint32_t left = heldMs >= FACTORY_RESET_HOLD_MS ? 0 : (FACTORY_RESET_HOLD_MS - heldMs + 999) / 1000;
    s_fb->fillRect(0, H - 22, W, 22, C_RED);
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextDatum(textdatum_t::middle_center);
    s_fb->setTextColor(C_TEXT);
    char t[40];
    snprintf(t, sizeof(t), "FACTORY RESET IN %lus", (unsigned long)left);
    s_fb->drawString(t, W / 2, H - 11);
}
}  // namespace

void uiInit() {
    s_lcd.init();
    s_lcd.setRotation(1);
    s_lcd.setBrightness(200);
    s_lcd.fillScreen(TFT_BLACK);
    s_fb = new LGFX_Sprite(&s_lcd);
    s_fb->setColorDepth(16);
    s_fb->setPsram(false);
    if (!s_fb->createSprite(W, H)) {
        delete s_fb;
        s_fb = nullptr;
    }
}

void uiRender(const UiState& s) {
    if (!s_fb) return;
    s_fb->fillScreen(C_BG);
    if (s.help) {
        drawHelp(s);
    } else if (s.phase == CameraPhase::Connected) {
        drawControls(s);
    } else {
        drawNoCamera(s);
    }
    drawResetBar(s.resetHeldMs);
    s_fb->pushSprite(0, 0);
}

void uiRenderSafeMode(uint32_t secondsLeft, uint32_t resetHeldMs) {
    if (!s_fb) return;
    s_fb->fillScreen(C_BG);
    s_fb->setFont(&fonts::FreeSansBold12pt7b);
    s_fb->setTextDatum(textdatum_t::top_center);
    s_fb->setTextColor(C_WARN);
    s_fb->drawString("SAFE MODE", W / 2, 10);
    s_fb->setFont(&fonts::Font2);
    s_fb->setTextColor(C_TEXT);
    s_fb->drawString("USB is a serial port for flashing.", W / 2, 44);
    s_fb->drawString("Network and remote are off.", W / 2, 62);
    char t[40];
    snprintf(t, sizeof(t), "Normal mode in %lus", (unsigned long)secondsLeft);
    s_fb->setTextColor(C_LABEL);
    s_fb->drawString(t, W / 2, 86);
    drawResetBar(resetHeldMs);
    s_fb->pushSprite(0, 0);
}
