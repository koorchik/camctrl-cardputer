#include "button.h"

#include <atomic>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logic/hold_detector.h"
#include "settings_store.h"

#ifndef BOARD_BUTTON_GPIO
#define BOARD_BUTTON_GPIO 0  // Cardputer: G0
#endif

namespace {
const char* TAG = "button";
std::atomic<uint32_t> s_held{0};
std::atomic<uint32_t> s_shortPresses{0};

void task(void*) {
    const gpio_num_t pin = (gpio_num_t)BOARD_BUTTON_GPIO;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    HoldDetector hold(FACTORY_RESET_HOLD_MS);
    bool wasPressed = false;
    uint32_t pressedAt = 0;
    while (true) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        const bool pressed = gpio_get_level(pin) == 0;
        if (pressed && !wasPressed) pressedAt = now;
        if (!pressed && wasPressed && now - pressedAt < SHORT_PRESS_MS) ++s_shortPresses;
        wasPressed = pressed;
        if (hold.update(pressed, now)) {
            ESP_LOGW(TAG, "factory reset");
            settingsErase();
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
        s_held = hold.heldMs(now);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}  // namespace

void buttonStart() {
    xTaskCreate(task, "button", 3072, nullptr, 3, nullptr);
}

uint32_t buttonHeldMs() {
    return s_held;
}

uint32_t buttonTakeShortPresses() {
    return s_shortPresses.exchange(0);
}
