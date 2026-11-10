#include "logic/keymap.h"

namespace {
constexpr char C = 0;  // marker: special key in kLayout, see kSpecial
const char kLayout[KEY_ROWS][KEY_COLS + 1] = {
    "`1234567890-=\b",
    "\tqwertyuiop[]\\",
    "\x01\x02" "asdfghjkl;'\n",
    "\x03\x04\x05" "zxcvbnm,./ ",
};
}  // namespace

Key keyAt(int row, int col) {
    if (row < 0 || row >= KEY_ROWS || col < 0 || col >= KEY_COLS) return {KeyCode::None, C};
    const char c = kLayout[row][col];
    switch (c) {
        case '\b': return {KeyCode::Backspace, C};
        case '\t': return {KeyCode::Tab, C};
        case '\n': return {KeyCode::Enter, C};
        case '\x01': return {KeyCode::Fn, C};
        case '\x02': return {KeyCode::Shift, C};
        case '\x03': return {KeyCode::Ctrl, C};
        case '\x04': return {KeyCode::Opt, C};
        case '\x05': return {KeyCode::Alt, C};
        default: return {KeyCode::Char, c};
    }
}

bool tcaDecode(uint8_t raw, KeyEvent& out) {
    const int number = raw & 0x7F;
    if (number == 0) return false;
    // TCA8418 numbers keys row * 10 + col + 1 over its 8 x 10 matrix; the
    // ADV wires a 7 x 8 matrix that M5Cardputer remaps like this.
    const int r = (number - 1) / 10;
    const int c = (number - 1) % 10;
    const int row = c % 4;
    const int col = r * 2 + (c > 3 ? 1 : 0);
    const Key k = keyAt(row, col);
    if (k.code == KeyCode::None) return false;
    out = {k, (raw & 0x80) != 0, (uint8_t)row, (uint8_t)col};
    return true;
}
