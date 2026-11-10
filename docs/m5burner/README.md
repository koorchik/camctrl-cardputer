# M5Burner listing

What to enter in M5Burner (USER CUSTOM → Publish, logged in). Use the **full** image from the
release (`tools/release.sh` → `dist/`); it is flashed at 0x0.

| Field | Value |
|---|---|
| Name | CamCtrl Cardputer |
| Version | 1.0.0 |
| Device type | Cardputer-ADV (ADV only: it needs the ADV keyboard controller) |
| GitHub | https://github.com/koorchik/camctrl-cardputer |
| Firmware | `camctrl-cardputer-cardputer-v1.0.0-full.bin` |
| Cover | `docs/m5burner/cover.png` (800 × 450, `tools/m5burner_cover.py`) |

## Description

**Handheld remote for Blackmagic cameras.**

A pocket remote for Blackmagic cameras. Plug the Cardputer ADV into the camera's USB‑C port: one‑press autofocus (Space or G0), and focus, iris, shutter, ISO, white balance, tint and clean feed on its screen and keyboard. Arrows pick a control, Enter opens it, left/right change, up/down big steps, h shows all keys. Shows the camera's battery and recording/tally state.

The Cardputer is also a Wi‑Fi bridge: join EthAir‑XXXX (password ethair123) with a phone and the CamCtrl app (camctrl.app) controls the camera at the same time.

IMPORTANT: set the Cardputer's power switch OFF before plugging it into the camera; it runs from the camera. Camera network: DHCP. Tested with a Micro Studio Camera 4K G2. Source and docs: https://github.com/koorchik/camctrl-cardputer
