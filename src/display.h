#pragma once

#include <cstdint>

// Status screen. On boards without a screen every call is a no-op.

constexpr int DISPLAY_LINES = 7;
constexpr int DISPLAY_LINE_CHARS = 40;

// RGB565 colours.
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GREY = 0x8410;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_CYAN = 0x07FF;

struct DisplayLine {
    char text[DISPLAY_LINE_CHARS + 1];
    uint16_t color;
};

struct DisplayModel {
    DisplayLine lines[DISPLAY_LINES];
};

void displayInit();
// Redraws only when the model changed.
void displayShow(const DisplayModel& model);
// printf into a line.
void displaySetLine(DisplayModel& m, int line, uint16_t color, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));
