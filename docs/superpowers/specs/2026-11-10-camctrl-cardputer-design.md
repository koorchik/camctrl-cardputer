# CamCtrl Cardputer: handheld camera remote on top of EthAir

## Why

The user films themselves (streams, outdoor recordings) with a Blackmagic Micro Studio Camera
4K G2. They want to hold a Cardputer ADV on a USB cable plugged into the camera and quickly
trigger autofocus and adjust the basic image controls without a phone.

The phone path must keep working: this firmware is EthAir (USB‑NCM ↔ Wi‑Fi bridge, DHCP,
settings page, OTA, safe mode) **plus** a remote UI on the Cardputer's screen and keyboard. It is
a separate firmware/repository so the base EthAir stays lean.

## Goals

- Everything EthAir does, unchanged: camera gets `192.168.73.2`, phones join `EthAir-XXXX`, the
  CamCtrl app works in parallel with the Cardputer.
- On the Cardputer: focus, autofocus, iris, ISO, white balance (+ auto WB), tint, and clean feed
  per video output (the camera reports `MainSDI`, `HDMI`, `FrontUSBC`).
- Fast and obvious in the field: autofocus on the biggest keys, values readable at a glance.
- Values follow changes made elsewhere (phone app, camera) via the camera's WebSocket.

## Non-goals

Recording control, shutter/gain/ND, monitoring overlays, presets, playback, the Flutter web app,
SD card features, the original (non‑ADV) Cardputer.

## Decisions (made autonomously; the user handed over design)

- **Platform:** ESP‑IDF 6.1 via PlatformIO, forked from EthAir (USB NCM via esp_tinyusb is not
  available in Arduino 2.0.17). Reused as is: bridge, DHCP, boot mode/safe mode, OTA, web,
  settings, logic/*.
- **Display:** M5GFX as an ESP‑IDF component with an explicit ST7789 config (no board autodetect,
  no M5Unified). Full‑screen sprite (240×135, RGB565) redrawn when the model changes.
- **Keyboard:** own TCA8418 driver (I2C 0x34 on GPIO8/9, INT GPIO11), mapped to the Cardputer
  4×14 layout. No Arduino libraries.
- **Camera access:** from the adapter's own interface (192.168.73.1) to the camera's REST API at
  the device IP (always 192.168.73.2 when the camera uses DHCP): `esp_http_client` for GET/PUT,
  `esp_websocket_client` for `/control/api/v1/event/websocket` with
  `{"type":"request","data":{"action":"subscribe","properties":[...]}}`. REST polling every 2 s
  while the WebSocket is down.

## Screen (240×135, landscape)

- Header: camera name (or `NO CAMERA` / `CONNECTING`), Wi‑Fi client count. A one‑line toast
  replaces it for ~1.5 s after actions (`AUTOFOCUS`, `AUTO WB`, errors).
- Six rows: `FOCUS 69%` (with a position bar), `IRIS f/1.4`, `ISO 400`, `WB 5600K`, `TINT +50`,
  `FEED SDI* HDMI USB` (outputs with clean feed on are highlighted; the selected output is
  underlined while the row is selected). The selected row is highlighted.
- No camera: the main area shows the EthAir info (SSID, password, `http://192.168.73.1`).
- Help (`h` or `?`): key map plus EthAir info, any key closes it.

## Keys

| Key | Action |
|---|---|
| `;` / `.` (the printed ↑/↓, no fn needed) | select row |
| `,` / `/` (←/→) | change value (one step); hold repeats with acceleration |
| Shift + ←/→ | big step |
| Space, G0 (short press) | autofocus |
| Enter | row action: FOCUS → autofocus, WB → auto WB, FEED → toggle selected output |
| `f` `i` `o` `w` `t` `c` | jump to FOCUS, IRIS, ISO, WB, TINT, FEED |
| `h`, `?` | help |
| G0 held 5 s | factory reset (EthAir behaviour) |

Steps: focus 0.5 % (big 5 %), iris one camera step via `adjustmentStep` (big 3), ISO one entry of
`supportedISOs` (big 3), WB 50 K (big 500 K) within `/video/whiteBalance/description`, tint 1 (big
10) within its description, FEED ←/→ selects the output.

## Camera protocol (verified on the Micro Studio Camera 4K G2, firmware 9.6.2)

| Control | Read | Write |
|---|---|---|
| Focus | `GET /lens/focus` → `{"normalised":0.69}`; `/lens/focus/description` → `controllable` | `PUT /lens/focus {"normalised":x}` (throttled to one per 50 ms, last value always sent) |
| Autofocus | – | `PUT /lens/focus/doAutoFocus {"position":{"x":0.5,"y":0.5}}`, re‑read focus after 500 ms |
| Iris | `GET /lens/iris` → `apertureStop`, `normalised`, `continuousApertureAutoExposure`; `/lens/iris/description` → `apertureStop.min/max`, `controllable` | `PUT /lens/iris {"adjustmentStep":n}`; one write in flight, the camera reports the new stop when the motor settles |
| ISO | `GET /video/iso` → `{"iso":400}`; `GET /video/supportedISOs` | `PUT /video/iso {"iso":v}` |
| WB | `GET /video/whiteBalance`; `/video/whiteBalance/description` (2500–10000) | `PUT /video/whiteBalance {"whiteBalance":k}`; auto: `PUT /video/whiteBalance/doAuto {}` |
| Tint | `GET /video/whiteBalanceTint`; `/description` (−50…50) | `PUT /video/whiteBalanceTint {"whiteBalanceTint":t}` |
| Clean feed | `GET /monitoring/display` → `{"displays":[...]}`; `GET /monitoring/{name}/cleanFeed` → `{"enabled":b}` | `PUT /monitoring/{name}/cleanFeed {"enabled":b}` |

WebSocket events are `{"type":"event","data":{"action":"propertyValueChanged","property":P,"value":{...}}}`
with the same value shapes as GET. Unknown keys mean "unchanged".

## Testing

- Native unit tests (`pio test -e native`) for the pure logic: key mapping, the remote model
  (selection, steps, clamping, row actions → commands), key repeat, camera JSON parsing.
- On hardware: flash over OTA through the phone on `EthAir-XXXX`; check every row against the
  camera and against the CamCtrl app changing the same values.

## Left to the builder

Exact fonts/colours, header details, repeat timings.
