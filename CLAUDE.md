# CLAUDE.md — CamCtrl Cardputer

Guidance for Claude Code (and humans) working in this repository.

## What this is

Firmware for the M5Stack Cardputer ADV only: the **EthAir** USB‑NCM ↔ Wi‑Fi bridge plus a handheld
**camera remote** on the Cardputer's screen and keyboard (focus/autofocus, iris, ISO, white
balance + auto WB, tint, clean feed per output). Built for Blackmagic cameras, tested with a Micro
Studio Camera 4K G2 (firmware 9.6.2). UI and docs are English.

- Bridge part = EthAir: adapter `192.168.73.1`, camera always `192.168.73.2`, Wi‑Fi `EthAir-XXXX`
  / `ethair123`, phones and the CamCtrl app keep working in parallel.
- The remote talks to the camera from the adapter's own lwIP netif: REST (`esp_http_client`) +
  events (`esp_websocket_client`, `/control/api/v1/event/websocket`).

Base project: **EthAir** (`../m5stack-ethernet-to-wifi`, GitHub `koorchik/ethair`). See "Keeping
the two firmwares in sync".

## Build, test, flash

```bash
~/.platformio/penv/bin/pio test -e native     # 100+ unit tests for src/logic (keep green)
~/.platformio/penv/bin/pio run -e cardputer   # the only device env (needs screen + keyboard)
tools/flash.sh [cardputer] [--usb]            # OTA via 192.168.73.1, else USB serial
```

- ESP‑IDF 6.1 via PlatformIO. Components (`src/idf_component.yml`): `espressif/esp_tinyusb`,
  `espressif/esp_websocket_client`, `espressif/cjson` (IDF 6 dropped the built‑in `json`),
  `m5stack/m5gfx`. Native tests compile the vendored `test/support/cjson`.
- Flashing paths, OTA self‑confirm (30 s + one HTTP request) and auto‑rollback (90 s), safe mode,
  ROM download: identical to EthAir — read its CLAUDE.md. With the Cardputer in the camera, flash
  through a phone on its Wi‑Fi using adb (`adb push` + `adb shell curl … /api/ota`).
- Debug: `/api/log` (heap is logged at startup and every 30 s), `/api/status` (`min_free_heap`).

## Architecture

| Path | Role |
|---|---|
| `src/main.cpp` | startup, the `Remote` input class (keys → `UiNav` → `RemoteModel` → camera), render pacing (30 fps while animating), OTA/heap watchdogs |
| `src/hw/keyboard.cpp` | TCA8418 driver (I2C0 GPIO8/9 @0x34, INT GPIO11), events → queue |
| `src/camera/camera_client.cpp` | camera REST/WebSocket; `shown` vs `actual` values, local holds (800 ms) + reconcile, coalesced writes (focus 50 ms throttle, iris one step batch in flight), AF/auto‑WB states, notices, tally/record |
| `src/ui/ui.cpp` | all screens (M5GFX), drawn into a 240×68 sprite twice per frame (`ui/canvas.h` shifts coordinates per band) |
| `src/ui/theme.*` | palette, Kelvin/tint LUTs, easing |
| `src/logic/` | pure logic + tests: `keymap`, `key_repeat`, `remote_model`, `ui_nav` (+`HintTracker`), `camera_json`, plus EthAir's forwarder/DHCP/mDNS/settings… |
| bridge files | same as EthAir (`bridge`, `dhcp_task`, `mdns_task`, `web`, `boot_mode`, `settings_store`, `log_buffer`) |

UI behaviour and every pixel decision: `docs/superpowers/specs/2026-11-10-remote-ui-design.md`.
Product/protocol decisions: `docs/superpowers/specs/2026-11-10-camctrl-cardputer-design.md`.

## Constraints that bite

- **RAM** (no PSRAM): never allocate a full‑screen 16 bpp sprite (64.8 KB). An earlier build that
  did, plus bigger stacks/buffers, left the bridge unable to allocate frames — the board joined
  Wi‑Fi but answered nothing, and needed a power cycle (rollback). Keep free heap ≥ ~40 KB at
  runtime; check `/api/log` heap lines after any change that allocates.
- **Main task stack** is 10240 bytes and big objects (`Remote`, `UiState`, JSON scratch) are
  `static`: a 3.5 KB default stack overflowed and two crashes put the board in safe mode. The boot
  guard counts crashes per firmware image, so a reflash clears the count.
- M5GFX is configured explicitly (`CardputerDisplay` in `ui.cpp`); no M5Unified, no autodetect
  (it would probe the I2C bus the keyboard driver owns). GPIO38 = backlight *and* RGB‑LED power:
  never drive it low.
- Keyboard arrows are the printed legends on `;` `.` `,` `/`, used without Fn.
- Camera quirks: iris reports only after the motor settles (one write in flight, `adjustmentStep`);
  `doAutoFocus`/`whiteBalance/doAuto` are PUT; clean feed is per display
  (`/monitoring/{name}/cleanFeed`, names from `/monitoring/display`); the camera answers its own mDNS
  name over the bridge.
- All EthAir hardware facts apply (Cardputer power switch OFF in the camera, no remote wakeup, USB
  handback details, no gateway for the USB side, Android 17 Nearby devices permission).

## Keeping the two firmwares in sync

Shared (port fixes in both directions, by diffing): `src/bridge.*`, `src/dhcp_task.*`,
`src/mdns_task.*`, `src/boot_mode.*`, `src/log_buffer.*`, `src/web.*`, `src/settings_store.*`
(here with extra UI flags), `src/logic/{forwarder,dhcp_server,mdns,settings,web_text,boot_guard,
frame_info,hold_detector}*`, `partitions.csv`, `tools/flash.sh` and their tests. Remote‑only:
`src/hw`, `src/camera`, `src/ui`, `src/logic/{keymap,key_repeat,remote_model,ui_nav,camera_*}`,
`src/main.cpp`, `src/button.*` (short press for AF). Keep the bridge code identical where possible
so a fix found in one firmware is a straight copy to the other.

## Conventions

- English commit messages, no `Co-Authored-By`; no usernames/home paths/personal data in code,
  docs or commits.
- UI text is English; keep hints compact (240×135).
- Repository is local only so far (no remote).
