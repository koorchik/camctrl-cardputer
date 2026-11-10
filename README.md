# EthAir: USB Ethernet → Wi‑Fi bridge

Firmware for an ESP32‑S3 (currently the M5Stack Cardputer ADV, later any ESP32‑S3 board with a
button). Plug it into any device with a USB host port that supports USB Ethernet adapters — for
example a Blackmagic Micro Studio Camera 4K G2 — and it powers itself from that port, shows up
there as a USB Ethernet adapter, and bridges that link to its own Wi‑Fi network. A phone on that
Wi‑Fi sees the device as if both were plugged into the same switch: mDNS discovery and direct IP
access both work.

```
device ──USB (CDC-NCM)──> [ESP32-S3: L2 bridge + DHCP] <──Wi-Fi── phone
                             192.168.73.1
device = 192.168.73.2                 phones = 192.168.73.10–50
```

## Using it

1. Set the USB device's network settings to **DHCP**.
2. Plug EthAir into the device's USB port.
3. Join the Wi‑Fi network **`EthAir-XXXX`** with password **`ethair123`** (XXXX is the end of the
   MAC address; the Cardputer shows it on screen).
4. Reach the device at **`192.168.73.2`** — it always gets this address.

The adapter hands out no gateway and no DNS: its network has no internet. The USB device never
gets a gateway either, so a computer used for setup keeps its own internet.

**Apps on the phone:**
- **iOS** — the app needs the *Local Network* permission.
- **Android 17+** — an app needs the **Nearby devices** permission (`ACCESS_LOCAL_NETWORK`) to
  reach local addresses at all. Chrome without it fails with `ERR_TOO_MANY_RETRIES`:
  Settings → Apps → Chrome → Permissions → Nearby devices → Allow.
- **Android, which network is "main"** — Android asks whether to stay on a Wi‑Fi without internet.
  If you keep it, EthAir becomes the main network: every app and browser reaches the device, but
  the phone has no internet while connected. If not, mobile data stays main: the internet works,
  and only apps that bind to the Wi‑Fi network (`ConnectivityManager.bindProcessToNetwork` or
  `Network.socketFactory`) reach the device. The adapter cannot change this choice.
- Verified on a Galaxy S24 Ultra (Android 17): settings page in Chrome and Samsung Internet,
  phone → adapter → USB host HTTP, ~585 KB/s for a 2 MB download.

**Cardputer with a camera: keep the power switch OFF.** With the battery on, the Cardputer
puts its own 5 V on the USB‑C port and the Blackmagic camera then never starts its USB host, so
the screen stays at `USB: no device`. Switched off, the Cardputer runs from the camera's port and
is found within ~20 s of plugging in. (A computer does not mind either way. A bare ESP32‑S3 board
without a battery is not affected.)

## Cardputer screen

```
EthAir-1C25                ← Wi-Fi name
pass ethair123             ← Wi-Fi password
cfg http://192.168.73.1    ← settings page
USB: device OK             ← USB device connected
dev 192.168.73.2           ← device IP (red if the device has a static IP from another network)
wifi clients 1
Hold G0 5s: factory reset
```

## Settings

Open `http://192.168.73.1/` from a phone or computer on the adapter's network: status, the
address plan, Wi‑Fi name, password, channel and the optional mDNS fallback name. Saving restarts
the adapter.

**Forgot the password?** On the Cardputer it is always on screen. On any board: hold **G0** for
5 seconds. Settings are erased and the adapter restarts as `EthAir-XXXX` / `ethair123`.

## mDNS

mDNS passes through the bridge untouched, so a device that announces itself (e.g.
`micro-studio-camera-4k-g2.local`) is discoverable from the phone as usual.

If the device does not answer for its name over USB Ethernet, set the **mDNS fallback name** on the
settings page (e.g. `micro-studio-camera-4k-g2`). The adapter then checks every 30 s whether the
device answers for that name and, only while it stays silent, answers on its behalf with the
device's IP. Empty (the default) turns this off. State: `"mdns": "off" | "device" | "adapter" |
"no_device"` in `/api/status`.

## API

| Method | Path | What it does |
|---|---|---|
| GET | `/api/status` | JSON: USB link, device IP, Wi‑Fi clients, frame counters, OTA state |
| GET | `/api/log` | last ~8 KB of logs (USB is busy being a network card, so there is no serial console) |
| POST | `/api/ota` | body = `firmware.bin`; network update with automatic rollback |
| POST | `/api/reboot?mode=normal\|safe\|download` | restart; `safe`/`download` hand USB back for flashing |

Both POST calls need the header `X-EthAir: 1` and the host `192.168.73.1`. This blocks web pages
the phone opens over mobile data from posting to the adapter (CSRF, DNS rebinding).

## Building and flashing

Needs PlatformIO. The framework is ESP‑IDF 6.1 (not Arduino: Arduino 2.0.17 has no TinyUSB network
class).

```bash
pio test -e native                # unit tests for the pure logic (DHCP, bridge, mDNS, settings…)
pio run -e cardputer              # Cardputer ADV (screen + G0)
pio run -e generic                # bare ESP32-S3, button on GPIO0 (BOARD_BUTTON_GPIO)
tools/flash.sh                    # build and flash: over the network (OTA) if reachable, else USB
tools/flash.sh cardputer --usb    # force USB serial
```

While the firmware runs, the USB‑C port is a network card, not a serial port. Ways to re‑flash:

1. **Over the network (normal):** a computer connected to the adapter over USB or Wi‑Fi reaches
   `192.168.73.1`, and `tools/flash.sh` uploads via `/api/ota`. A new image is kept only after it
   has run for 30 s and served an HTTP request; otherwise the bootloader rolls back.
2. **Safe mode:** `curl -X POST -H "X-EthAir: 1" "http://192.168.73.1/api/reboot?mode=safe"` — the
   board restarts with USB as a serial port (`/dev/ttyACM0`), then `tools/flash.sh cardputer --usb`.
   Without flashing, safe mode returns to normal by itself after 3 minutes. Two crashes in a row
   also start safe mode.
3. **By hand (always works):** switch the Cardputer off, hold **G0**, switch on, release — the ROM
   download mode. Then `tools/flash.sh cardputer --usb`, then switch off and on again (without G0):
   a reset from the computer does not re‑read the G0 strap.

## Layout

| File | Role |
|---|---|
| `src/bridge.cpp` | L2 bridge: TinyUSB NCM ↔ Wi‑Fi AP ↔ the adapter's own lwIP interface |
| `src/dhcp_task.cpp` | DHCP server (USB device always `.2`, no gateway/DNS) |
| `src/mdns_task.cpp` | optional mDNS fallback responder |
| `src/web.cpp` | settings page, JSON API, OTA |
| `src/boot_mode.cpp` | safe mode, handing USB back to serial/JTAG |
| `src/display.cpp`, `src/button.cpp` | Cardputer screen (esp_lcd ST7789), G0 button |
| `src/logic/` | pure logic without ESP‑IDF, covered by `test/` |

The original design notes (written while this was a Blackmagic‑only adapter) are in
`docs/superpowers/specs/`.

## Limits and notes

- Tested with a Linux PC and with a Blackmagic Micro Studio Camera 4K G2 (firmware 9.6.2): the
  camera accepts the class‑compliant USB NCM adapter, gets `192.168.73.2`, answers its REST API
  and its own mDNS name through the bridge. Other devices must be checked individually.
- No remote wakeup is advertised: with it, the camera autosuspended the idle adapter and frames
  for the camera were lost. Without VBUS sensing a pulled cable looks like a bus suspend, so a
  link suspended for 3 s is shown as down and the adapter re‑attaches itself every 10 s. If not, the next step is
  ECM/RNDIS mode (`CONFIG_TINYUSB_NET_MODE_ECM_RNDIS`; esp_tinyusb ships no default descriptors
  for it, so they would have to be written).
- The factory password is the same on every adapter (`ethair123`) so it can be known on boards
  without a screen. Change it on the settings page if strangers may be nearby.
- The settings page and OTA have no separate password; the Wi‑Fi password protects them.
- Up to 4 Wi‑Fi clients at a time.
