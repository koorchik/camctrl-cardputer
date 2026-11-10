#pragma once

#include <cstdint>

constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;

// Polls the board button (G0). Holding it FACTORY_RESET_HOLD_MS erases
// settings and restarts; a short press (released within SHORT_PRESS_MS) is
// counted for the remote (autofocus).
constexpr uint32_t SHORT_PRESS_MS = 600;

void buttonStart();

// How long the button has been held right now (0 when released).
uint32_t buttonHeldMs();

// Short presses since the last call.
uint32_t buttonTakeShortPresses();
