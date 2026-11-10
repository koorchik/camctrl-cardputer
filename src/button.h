#pragma once

#include <cstdint>

constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;

// Polls the board button. Holding it FACTORY_RESET_HOLD_MS erases settings
// and restarts.
void buttonStart();

// How long the button has been held right now (0 when released).
uint32_t buttonHeldMs();
