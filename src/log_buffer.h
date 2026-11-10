#pragma once

#include <cstddef>

// Keeps the latest log output in RAM so it can be read over the network
// (GET /api/log) while USB is busy being a network adapter.
void logBufferInit();

// Copies the buffered log (oldest first) into `out`; returns bytes written.
size_t logBufferRead(char* out, size_t cap);
