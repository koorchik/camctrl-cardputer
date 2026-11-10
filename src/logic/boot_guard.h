#pragma once

#include <cstdint>

// Decides whether to boot in safe mode: USB stays the serial/JTAG port (so the
// board can be flashed without pressing buttons) and the network is off.
// Pure logic; the state lives in RTC memory that survives software resets.

constexpr uint32_t BOOT_GUARD_MAGIC = 0xB0C4A11Eu;
constexpr uint32_t BOOT_GUARD_CRASH_LIMIT = 2;
constexpr uint32_t BOOT_GUARD_STABLE_MS = 60000;

enum class ResetKind { PowerOn, Software, Crash, Other };

struct BootGuardState {
    uint32_t magic;
    uint32_t crashCount;
    uint32_t safeRequested;
    uint32_t firmwareId;  // which image counted the crashes
};

// Updates `st` for this boot and returns true if this boot is in safe mode.
// Power-on resets clear everything; crashes count up; a safe-mode request
// (made before a software reset) is honoured once.
// `firmwareId` identifies the running image: crashes counted by another
// image (i.e. before a reflash) do not count against this one.
bool bootGuardOnBoot(BootGuardState& st, ResetKind reset, uint32_t firmwareId);

// Call once the firmware has been running for BOOT_GUARD_STABLE_MS.
void bootGuardMarkStable(BootGuardState& st);

// Asks the next boot to be a safe-mode boot.
void bootGuardRequestSafe(BootGuardState& st);
