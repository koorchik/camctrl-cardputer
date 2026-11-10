# CamCtrl Cardputer: handheld remote for Blackmagic cameras

Firmware for the M5Stack Cardputer ADV. Plug it into a Blackmagic camera's USB‑C port (tested with a
Micro Studio Camera 4K G2) and hold it in your hand: autofocus with one key, and adjust focus,
iris, ISO, white balance, tint and clean feed on the Cardputer's own screen and keyboard.

It is the [EthAir](https://github.com/koorchik/ethair) firmware (USB Ethernet ↔ Wi‑Fi bridge)
plus the remote. Everything EthAir does still works at the same time: the camera gets
`192.168.73.2`, phones join `EthAir-XXXX` (password `ethair123`) and the CamCtrl phone app can
control the camera in parallel; both see each other's changes through the camera's event stream.

```
camera ──USB (NCM)──> [Cardputer: bridge + remote UI] <──Wi-Fi── phone app (optional)
                        192.168.73.1
```

## Using it

1. Camera network settings: **DHCP**.
2. **Set the Cardputer's power switch to OFF** and plug the USB cable into the camera. With the
   battery on, the Cardputer puts 5 V on the port and the camera never starts its USB host (after
   15 s the screen says so).
3. The connection screen walks through USB → NETWORK → CAMERA. On the first connection a two‑card
   intro shows the keys.

### Keys

| Key | Action |
|---|---|
| `;` `.` `,` `/` (printed ↑ ↓ ← →) | move over the two columns of controls |
| Enter | open the selected control |
| ← → (control open) | change it; hold = fast |
| ↑ ↓ (control open) | big steps (focus ×10, iris ×3, ISO 1 stop, WB 500 K, tint ×10); Shift + ← → too |
| Space, side button **G0** | **autofocus** |
| Enter (control open) | focus: AF · WB: auto white balance · feed: toggle the selected output |
| `f` `i` `o` `w` `t` `c` | jump to focus, iris, ISO, WB, tint, clean feed |
| Del / `` ` `` | back to the overview (also by itself after 3 s idle) |
| `h` | all keys; `h` again: Wi‑Fi details (Enter there replays the intro) |
| G0 held 5 s | factory reset (a countdown appears after 1 s; release to cancel) |

The screen has no key strip: press `h` for the keys (a second `h` shows the Wi‑Fi details). An
unknown key pressed twice reminds you of `h`.

### Screens

- **Status bar**: camera name, REC / tally, the camera's battery (level, % and a bolt while
  charging; `AC` when it runs from mains without a battery), live link, phones on Wi‑Fi.
- **Overview**: six tiles in two columns (FOCUS, IRIS, ISO | WB, TINT, CLEAN FEED) with values and
  small gauges.
- **Detail views** (open while adjusting, close after 3 s idle): a focus tape with the camera's
  real position and an AF viewfinder, an iris with closing blades and the lens's f‑stop range, an
  ISO drum with an EV counter, a Kelvin colour bar with preset icons and an auto‑WB scan, a tint bar
  with a grey card, and per‑output mini monitors for clean feed.
- Amber values are sent but not yet confirmed by the camera; white values are what the camera
  reports; a blue flash means the value was changed elsewhere (e.g. the phone app). A red frame
  means the camera is recording / on program.

## Building and flashing

ESP‑IDF 6.1 via PlatformIO (M5GFX as an ESP‑IDF component, own TCA8418 keyboard driver).

```bash
pio test -e native             # pure logic: key map, remote model, navigation, camera JSON, DHCP, bridge…
pio run -e cardputer
tools/flash.sh                 # over the network (OTA) when reachable at 192.168.73.1, else USB serial
tools/flash.sh cardputer --usb
```

Flashing paths, safe mode and automatic rollback are the same as in EthAir (see its README):
an OTA image that does not confirm itself within 90 s restarts and rolls back; two crashes in a
row start safe mode (USB becomes a serial port for 3 minutes). With the Cardputer in the camera,
OTA works from any machine on its Wi‑Fi (e.g. a phone: `curl -H "X-EthAir: 1" --data-binary
@firmware.bin http://192.168.73.1/api/ota`).

## Design documents

- `docs/superpowers/specs/2026-11-10-camctrl-cardputer-design.md` — goals, protocol, keys.
- `docs/superpowers/specs/2026-11-10-remote-ui-design.md` — the UI spec (layouts, colours, states).
- `docs/superpowers/specs/2026-11-10-usb-ethernet-wifi-adapter-design.md` — the EthAir base.
