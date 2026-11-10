#pragma once

#include <cstdint>

// Cardputer ADV keyboard: TCA8418 key events mapped to the Cardputer's
// 4 x 14 layout. Pure logic.

enum class KeyCode : uint8_t { None, Char, Enter, Backspace, Tab, Fn, Shift, Ctrl, Opt, Alt };

struct Key {
    KeyCode code;
    char ch;  // for KeyCode::Char (space is ' ')
};

struct KeyEvent {
    Key key;
    bool pressed;
    uint8_t row;
    uint8_t col;
};

constexpr int KEY_ROWS = 4;
constexpr int KEY_COLS = 14;

// Key at a layout position (row 0 = number row). Out of range -> None.
Key keyAt(int row, int col);

// Decodes one TCA8418 KEY_EVENT_A byte (bit 7 = pressed, low 7 bits = key
// number 1..80). Returns false for 0 (FIFO empty) or a position outside the
// layout.
bool tcaDecode(uint8_t raw, KeyEvent& out);
