#pragma once

#include <M5GFX.h>

// Draws a 240 x 135 frame in horizontal bands through one smaller sprite
// (RAM is tight: a full-screen 16 bpp sprite would take 64 KB). All drawing
// uses screen coordinates; the canvas shifts them into the current band and
// the sprite clips the rest. The frame is drawn once per band.
class Canvas {
public:
    explicit Canvas(LGFX_Sprite* s) : s_(s) {}
    void setBand(int top) { oy_ = top; }
    int bandTop() const { return oy_; }
    LGFX_Sprite* sprite() { return s_; }

    void fillScreen(uint16_t c) { s_->fillScreen(c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) { s_->fillRect(x, y - oy_, w, h, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) { s_->drawRect(x, y - oy_, w, h, c); }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { s_->fillRoundRect(x, y - oy_, w, h, r, c); }
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { s_->drawRoundRect(x, y - oy_, w, h, r, c); }
    void fillCircle(int x, int y, int r, uint16_t c) { s_->fillCircle(x, y - oy_, r, c); }
    void drawCircle(int x, int y, int r, uint16_t c) { s_->drawCircle(x, y - oy_, r, c); }
    void drawFastHLine(int x, int y, int w, uint16_t c) { s_->drawFastHLine(x, y - oy_, w, c); }
    void drawFastVLine(int x, int y, int h, uint16_t c) { s_->drawFastVLine(x, y - oy_, h, c); }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { s_->drawLine(x0, y0 - oy_, x1, y1 - oy_, c); }
    void drawWideLine(float x0, float y0, float x1, float y1, float r, uint16_t c) {
        s_->drawWideLine(x0, y0 - oy_, x1, y1 - oy_, r, c);
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
        s_->fillTriangle(x0, y0 - oy_, x1, y1 - oy_, x2, y2 - oy_, c);
    }
    void fillArc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) {
        s_->fillArc(x, y - oy_, r0, r1, a0, a1, c);
    }
    void drawArc(int x, int y, int r0, int r1, float a0, float a1, uint16_t c) {
        s_->drawArc(x, y - oy_, r0, r1, a0, a1, c);
    }
    void setClipRect(int x, int y, int w, int h) { s_->setClipRect(x, y - oy_, w, h); }
    void clearClipRect() { s_->clearClipRect(); }

    void setFont(const lgfx::IFont* f) { s_->setFont(f); }
    void setTextDatum(textdatum_t d) { s_->setTextDatum(d); }
    void setTextColor(uint16_t c) { s_->setTextColor(c); }
    int textWidth(const char* t) { return s_->textWidth(t); }
    int fontHeight() { return s_->fontHeight(); }
    // Returns the x after the text for left datums (width added).
    int drawString(const char* t, int x, int y) { return s_->drawString(t, x, y - oy_); }

private:
    LGFX_Sprite* s_;
    int oy_ = 0;
};
