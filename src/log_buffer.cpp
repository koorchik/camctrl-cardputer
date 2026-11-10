#include "log_buffer.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace {
constexpr size_t RING_SIZE = 8192;
char s_ring[RING_SIZE];
size_t s_head = 0;  // next write position
bool s_wrapped = false;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_next = nullptr;

int hook(const char* fmt, va_list args) {
    char line[192];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        portENTER_CRITICAL_SAFE(&s_lock);
        for (size_t i = 0; i < len; ++i) {
            s_ring[s_head++] = line[i];
            if (s_head == RING_SIZE) {
                s_head = 0;
                s_wrapped = true;
            }
        }
        portEXIT_CRITICAL_SAFE(&s_lock);
    }
    return s_next ? s_next(fmt, args) : n;
}
}  // namespace

void logBufferInit() {
    s_next = esp_log_set_vprintf(hook);
}

size_t logBufferRead(char* out, size_t cap) {
    if (cap == 0) return 0;
    portENTER_CRITICAL_SAFE(&s_lock);
    size_t n = 0;
    if (s_wrapped) {
        for (size_t i = s_head; i < RING_SIZE && n + 1 < cap; ++i) out[n++] = s_ring[i];
    }
    for (size_t i = 0; i < s_head && n + 1 < cap; ++i) out[n++] = s_ring[i];
    portEXIT_CRITICAL_SAFE(&s_lock);
    out[n] = '\0';
    return n;
}
