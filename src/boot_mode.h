#pragma once

// Safe mode keeps the USB port as the chip's serial/JTAG port so the board can
// be re-flashed without pressing buttons. See logic/boot_guard.h.

constexpr unsigned SAFE_MODE_TIMEOUT_MS = 180000;

// Decides this boot's mode. Call first thing in app_main. In safe mode the
// USB pads are handed back to serial/JTAG right away.
bool bootModeIsSafe();

// Call once the firmware has run long enough to count as stable.
void bootModeMarkStable();

// Hands the USB pads back to the serial/JTAG controller (host re-enumerates).
void bootModeReconnectSerialJtag();

// Reboots with USB as serial/JTAG: into safe mode, normally, or straight into
// the ROM download mode (esptool --before no_reset).
[[noreturn]] void bootModeRestartSafe();
[[noreturn]] void bootModeRestartNormal();
[[noreturn]] void bootModeRestartDownload();
