#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "logic/keymap.h"

// Cardputer ADV keyboard (TCA8418 on the internal I2C bus). Key presses and
// releases arrive on the queue as KeyEvent.

// Starts the driver task. Returns the event queue, or nullptr if the
// keyboard controller does not answer.
QueueHandle_t keyboardStart();

// True while a Shift key is held.
bool keyboardShiftHeld();
