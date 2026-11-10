# CamCtrl Cardputer: remote UI design

Design spec for the on-device UI of the handheld camera remote (Cardputer ADV, 240 x 135 ST7789,
M5GFX full-screen sprite). It replaces the "Screen" section of
`2026-11-10-camctrl-cardputer-design.md`. Everything here is drawable with M5GFX primitives and
built-in fonts into the single 240 x 135 x 16 bpp sprite that is already allocated in `ui.cpp`.

> **Revision (2026-11-10, after the first hands-on test).** The overview is navigated as a grid,
> which supersedes the key bindings, hint bars, help rows and intro card 2 drawn below:
> - Overview: `;` `.` move within a column, `,` `/` switch columns (edges stop, no wrap); Enter
>   opens the selected control. Arrows no longer change values on the overview.
>   Hint bar: `[;^][.v][,<][/>] pick  [ENT] open  [SPC] AF`.
> - Detail (control open): `,` `/` change, `;` `.` big step (the Shift step), Enter = the
>   control's action (AF / auto WB / feed toggle), Del or `` ` `` back, 3 s idle closes. The
>   `[SHIFT]` hint caps are replaced by `[;^][.v]` and the trailing `[;^][.v]` by `[DEL]`.
> - Status bar: the camera's battery from `/camera/power` (polled on connect and every 10 s, also
>   subscribed) sits left of the LIVE dot: percentage, a 15 x 8 battery icon with a level bar
>   (`OK`, `WARN` ≤ 30 %, `ERR` ≤ 15 %, `INFO` while charging), a bolt while charging; the
>   source name (`AC`, `USB`) when no battery is present. The camera name is clipped before it.

All coordinates are absolute screen pixels, origin top-left, `x` 0..239, `y` 0..134. A rectangle
`(x, y, w, h)` covers `x..x+w-1`, `y..y+h-1`. "Baseline (x, y)" means `textdatum_t::baseline_left`
(or `baseline_right` / `baseline_center` where stated).

---

## 1. Design principles

1. **Camera OSD, not a menu.** Black field, white values, one warm accent (amber) for "what you
   are touching", status colours only for status. No decorative gradients on surfaces; gradients
   appear only where they carry meaning (Kelvin, tint).
2. **Two modes, one mental model.** `▲▼` = *navigate* (overview of all six controls).
   `◄►` = *adjust* (a full-screen, control-specific detail view). Releasing the keys returns to the
   overview by itself.
3. **Readable at arm's length outdoors.** Pure black background, no value smaller than
   FreeSansBold 12 pt, no critical information in grey, every state shown by **shape or text as
   well as colour** (glare and colour blindness). Backlight at 255.
4. **Commanded vs actual is always visible.** Amber = value we sent, not yet confirmed. White =
   value the camera reports. A short green flash = "camera confirmed".
5. **Self-teaching.** Drawn key caps with the printed arrow next to them are on screen at all
   times, contextual to the selected control; a two-card intro runs on first connect; the full key
   map is one `h` away.

---

## 2. Visual language

### 2.1 Palette (RGB888 -> RGB565)

| Token | RGB | RGB565 | Use |
|---|---|---|---|
| `BG` | 0,0,0 | `0x0000` | screen background |
| `STATUS_BG` | 14,15,18 | `0x0862` | status bar, hint bar |
| `PANEL` | 20,22,27 | `0x10A3` | tiles, tapes, scales |
| `PANEL_HI` | 34,37,45 | `0x2125` | selected tile fill, key caps, windows |
| `LINE` | 60,64,74 | `0x3A09` | hairlines, minor ticks, hatching, key-cap border |
| `TEXT` | 255,255,255 | `0xFFFF` | values (confirmed), major ticks |
| `TEXT_2` | 176,182,194 | `0xB5B8` | labels, secondary text, hint words |
| `TEXT_3` | 96,100,110 | `0x632D` | disabled, "--", tertiary marks |
| `ACCENT` | 255,190,0 | `0xFDE0` | selection, index marks, pending (commanded) values |
| `ACCENT_DIM` | 110,82,0 | `0x6A80` | pending chip outline, slim-hint accents |
| `OK` | 40,224,100 | `0x2F0C` | confirmed flash, AF OK, clean feed ON, live link |
| `INFO` | 0,200,255 | `0x065F` | operation in progress (AF, auto WB), external change |
| `WARN` | 255,128,0 | `0xFC00` | polling, unavailable control, no reply |
| `ERR` | 240,40,40 | `0xF145` | write failed / 403, REC / PGM tally, factory reset |
| `PVW` | 0,210,70 | `0x0688` | preview tally |
| `LIGHT` | 255,238,196 | `0xFF78` | light through the iris opening |
| `BLADE` | 46,50,60 | `0x2987` | iris housing / blades |
| `TINT_G` | 40,200,80 | `0x2E4A` | tint -50 |
| `TINT_N` | 72,72,72 | `0x4A49` | tint 0 |
| `TINT_M` | 230,50,200 | `0xE199` | tint +50 |
| `SKY` / `GROUND` / `SUN` / `SKIN` | 40,80,130 / 40,90,50 / 255,210,80 / 220,170,130 | `0x2A90` / `0x2AC6` / `0xFE8A` / `0xDD50` | feed mini-monitor picture |

Kelvin gradient stops (stylised, more saturated than physical blackbody so it reads on a small
screen; linear interpolation per channel in Kelvin between stops):

| K | 2500 | 3200 | 4000 | 5600 | 6500 | 7500 | 10000 |
|---|---|---|---|---|---|---|---|
| RGB | 255,120,20 | 255,160,60 | 255,200,130 | 255,246,236 | 225,236,255 | 170,200,255 | 80,140,255 |

Precompute at `uiInit()`: `uint16_t kelvinLut[220]` (x = 0..219 maps to 2500..10000 K linearly) and
`uint16_t tintLut[220]` (-50..+50, `TINT_G` -> `TINT_N` -> `TINT_M`). 880 bytes total.

Helper: `uint16_t blend565(uint16_t a, uint16_t b, float t)` (per-channel lerp, t clamped 0..1) is
used for every fade/flash below.

### 2.2 Typography

| Role | Font | Notes |
|---|---|---|
| Hero value (detail views) | `FreeSansBold24pt7b` | digits ~34 px tall, advance 27 px |
| Hero unit / prefix (`%`, `K`, `f/`) | `FreeSansBold12pt7b` | same baseline as hero |
| ISO drum centre value | `FreeSansBold18pt7b` | advance ~20 px; `25600` ~100 px |
| Tile values (overview) | `FreeSansBold12pt7b` | fallback `FreeSansBold9pt7b` if `textWidth > 84` |
| Screen titles (connection, onboarding) | `FreeSansBold9pt7b` | |
| Detail title, body text, chips (large) | `Font2` | |
| Labels, key-cap legends, scale numbers, status bar | `Font0` | 6 x 8, always at size 1 |
| Countdown digit (factory reset) | `Font7` | 7-segment, 48 px |

Rules: labels UPPERCASE in `Font0`/`Font2`; values never in `Font0`; at most one 24 pt element per
screen.

### 2.3 Reusable components

**Key cap** `drawKeycap(x, y, legend, arrow)` (arrow: none / up / down / left / right):
- size: `w = 6*len(legend) + 6 + (arrow ? 7 : 0)`, `h = 11`.
- `fillRoundRect(x, y, w, 11, 2, PANEL_HI)`, `drawRoundRect(x, y, w, 11, 2, LINE)`,
  1 px "key depth" line `drawFastHLine(x+2, y+11, w-4, LINE)` (so caps are 12 px tall in total).
- legend `Font0` `TEXT`, top-left at `(x+3, y+2)`.
- arrow: a filled triangle in `ACCENT`, 5 x 5 px, its box at `(x+3+6*len+2, y+3)`:
  up `(0,4)(4,4)(2,0)`, down `(0,0)(4,0)(2,4)`, left `(4,0)(4,4)(0,2)`, right `(0,0)(0,4)(4,2)`.
  The arrow is drawn because the arrows are printed on those keys; the legend char is what the
  user can find on the key top.
- Dim variant (slim hints): fill `STATUS_BG`, border `LINE`, legend `TEXT_2`, arrow `ACCENT_DIM`.

**Hint item**: one or more key caps (2 px apart), then 3 px, then a word in `Font0` `TEXT_2`.
Items are separated by 6 px. A hint bar is laid out left to right from `x = 4`, and every hint set
in this spec has been checked to fit in 232 px.

**Chip** `drawChip(xRight, y, text, style)`: `Font0` text, `h = 11`, `w = textWidth + 8`,
right-aligned at `xRight`. Styles: *filled* (`fillRoundRect` r3 in colour, text `BG`), *outline*
(`drawRoundRect` r3 in colour, text in colour).

**Marker triangle** (pointer onto a scale): `fillTriangle` 10 px wide, 8 px tall, apex on the
scale; plus a 1 px `BG` outline line on each slanted edge when drawn over a gradient.

**Animated value** `approach(cur, target, dtMs, halfLifeMs)`:
`cur += (target - cur) * (1 - exp2f(-dtMs / halfLifeMs))`, snap to target when within 0.25 px
(or 0.001 of the normalised value). Every moving marker, tape and drum uses this; nothing uses
fixed-duration tweens except the flashes.

---

## 3. Global frame

### 3.1 Status bar (all screens with a camera connected) `(0, 0, 240, 14)` fill `STATUS_BG`

```
x 0                                                                     239
 [REC][PGM] Micro Studio 4K G2             * LIVE  []2  [h]
```

Left to right, all `Font0`, text top at `y = 3`:
- **Tally / record pills** (only if the camera reports them, see 9.1), `h = 10` at `y = 2`, r3:
  `REC` = fill `ERR`, text `TEXT`, preceded by a 2 px-radius white dot (pill `w = 30`);
  `PGM` = fill `ERR`, text `TEXT` (`w = 24`); `PVW` = fill `PVW`, text `BG` (`w = 24`).
  First pill at `x = 3`, 3 px apart.
- **Camera name** in `TEXT_2`, after the pills (or at `x = 4`). Max right edge `x = 150`. If too
  long, drop the word `Camera` first ("Micro Studio Camera 4K G2" -> "Micro Studio 4K G2"), then cut
  characters. Empty name -> `CAMERA`.
- Right side, laid out right to left from `x = 237`:
  - `[h]` key cap (dim variant) at `x = 225`, `y = 1`. Always present: this is the discoverable
    entry to help.
  - **Phones**: 5 x 8 px phone outline (`drawRoundRect(x, 3, 5, 8, 1, TEXT_2)` + 1 px dot at the
    bottom) and the count, e.g. `2`, in `TEXT_2`. Hidden when 0.
  - **Link**: filled circle r3 + word. WebSocket events flowing: `OK` dot + `LIVE` in `TEXT_2`.
    Polling: `WARN` dot + `POLL` in `WARN`; the dot blinks (500 ms on/off) for the first 5 s after
    the drop.

### 3.2 Tally border

When the camera is recording or on Program: `drawRect(0,0,240,135,ERR)` and `drawRect(1,1,238,133,ERR)`
drawn last (over everything except the factory-reset overlay). Preview: same in `PVW`. All layouts
below keep the outer 2 px (x 0..1, 238..239, y 133..134) free of content so the border never
covers information; it overlays only the status bar's first two rows.

### 3.3 Hint bar `(0, 121, 240, 12)` fill `STATUS_BG`

Key caps at `y = 121` (12 px with depth line), words top at `y = 123`. Contents depend on screen,
selected control and hint level (section 8).

### 3.4 Banner (toast)

Replaces the status bar for 1500 ms (errors 2500 ms). `(0, 0, 240, 18)`, slides in from
`y = -18` to `0` over 80 ms (`approach`, half-life 25 ms), drawn on top of the content (the
content below is a tile/title margin, so covering 4 extra px is harmless). Text `Font2`, centred at
`(120, 9)`, `textdatum_t::middle_center`.

| Kind | Fill | Text | Examples |
|---|---|---|---|
| info | `INFO` | `BG` | `AUTOFOCUS`, `AUTO WHITE BALANCE` |
| ok | `OK` | `BG` | `LIVE LINK RESTORED` |
| warn | `WARN` | `BG` | `IRIS ON AUTO EXPOSURE`, `LENS NOT CONTROLLABLE`, `NO REPLY FROM CAMERA`, `LIVE LINK LOST - POLLING` |
| error | `ERR` | `TEXT` | `WRITE FAILED`, `NOT ALLOWED (403)` |

A newer banner replaces an older one immediately (no queue).

---

## 4. Overview (home) screen

Replaces the 6-row list with a **2 x 3 tile grid**: column 1 = lens & exposure
(FOCUS, IRIS, ISO), column 2 = colour & output (WB, TINT, FEED). The existing linear order
Focus -> Iris -> Iso -> Wb -> Tint -> Feed is kept, so `▼` moves down a column and wraps from ISO to
the top of column 2 (and from FEED to FOCUS); `▲` reverses. No change to `RemoteModel`.

```
+--------------------------------------------------------------------------+
|[REC] Micro Studio 4K G2                         * LIVE  []2  [h]         |  y 0..13
| +-FOCUS [f]--------------------+  +-WB [w]------------------------+       |
| | [::]                  69.5%  |  | (sun)                  5600K  |       |  y 15..47
| | =====================|=====  |  | warm>>>>>>>>>>>>>>>>|>>>>cool |       |
| +------------------------------+  +-------------------------------+       |
| +-IRIS [i]---------------------+  +-TINT [t]----------------------+       |
| | (o)                   f/2.8  |  |                         +12   |       |  y 51..83
| | ======|======================|  | green-------|---+--magenta    |       |
| +------------------------------+  +-------------------------------+       |
| +-ISO [o]----------------------+  +-CLEAN FEED [c]---------------+       |
| |                         800  |  | [ SDI ] [HDMI ] [ USB ]       |       |  y 87..119
| | .......:.....|.....:.....:.. |  |  green = clean                |       |
| +------------------------------+  +-------------------------------+       |
| [; ^][. v] pick  [, <][/ >] adjust  [SPC] AF                              |  y 121..132
+--------------------------------------------------------------------------+
```

### 4.1 Tile geometry

| Tile | x | y | w | h |
|---|---|---|---|---|
| FOCUS | 3 | 15 | 115 | 33 |
| IRIS | 3 | 51 | 115 | 33 |
| ISO | 3 | 87 | 115 | 33 |
| WB | 122 | 15 | 115 | 33 |
| TINT | 122 | 51 | 115 | 33 |
| FEED | 122 | 87 | 115 | 33 |

Inside each tile (offsets from the tile origin `tx, ty`):
- Background `fillRoundRect(tx, ty, 115, 33, 4, PANEL)`.
- **Label** `Font0` `TEXT_2` top-left `(tx+6, ty+3)`: `FOCUS`, `IRIS`, `ISO`, `WB`, `TINT`,
  `CLEAN FEED`.
- **Jump key hint**: 9 x 10 mini key cap right after the label (`x = labelRight + 4`, `y = ty+2`),
  `drawRoundRect` r2 `LINE`, lowercase letter `Font0` `TEXT_3` centred: `f i o w t c`. Teaches the
  letter jumps without the help screen.
- **State badge** (optional, top-right), chip right edge `tx+110`, `y = ty+2`: `AUTO` (INFO
  outline) for iris under AE, a padlock glyph (see 6.4) for not controllable, `NO LENS` (TEXT_3
  outline) for `irisStop == 0`, `AF` (INFO filled, pulsing) during autofocus.
- **Glyph** 16 x 16 at `(tx+6, ty+12)` (see below).
- **Value** `FreeSansBold12pt7b`, `baseline_right` at `(tx+109, ty+26)`. Colour by state
  (section 6.1). Unknown -> `--` in `TEXT_3`.
- **Gauge** 2 px tall at `(tx+6, ty+29, 103, 2)` (see below).

Selected tile: fill `PANEL_HI`, `drawRoundRect` r4 twice (`(tx,ty,115,33)` and
`(tx+1,ty+1,113,31)`) in `ACCENT`, label in `ACCENT`. Selection change animates: the amber frame
moves from the old tile rect to the new one (`approach` on x, y, half-life 30 ms; ~4 frames).

### 4.2 Per-tile glyphs, values and gauges

| Tile | Glyph (16 x 16 box at `gx, gy`) | Value text | Gauge (2 px, 103 px wide) |
|---|---|---|---|
| FOCUS | AF frame: four L-corners, arm 4 px, 1 px, box `(gx+1, gy+2, 14, 12)`, `TEXT_3`; centre dot. `INFO` while AF runs, `OK` for 1.2 s after | `69.5%` (one decimal) | track `LINE`; position tick 3 x 4 px (`ty+28..31`) `TEXT` at `focus * 102` |
| IRIS | live aperture: `fillCircle(gx+8, gy+8, 7, BLADE)`, opening = hexagon radius `1.5 + 5.5 * (irisMin/irisStop)` in `LIGHT` | `f/2.8` (`f/4` for whole numbers, `f/16.4`) | track `LINE`, tick `TEXT` at normalised iris |
| ISO | none | `800` | 25 dots (one per supported ISO, 1 px every 4 px) in `LINE`, current one 3 x 4 px `TEXT` |
| WB | nearest preset icon (6.3) scaled to 14 px, drawn in its Kelvin colour; none if no preset within 150 K | `5600K` (`K` in the same font) | the Kelvin gradient (sampled from `kelvinLut` every 2nd entry), tick 2 x 4 px `TEXT` with 1 px `BG` either side |
| TINT | none | `+12`, `0`, `-7` | the tint gradient, 1 px centre tick `TEXT_2` always, current tick `TEXT` |
| FEED | none (no value) | — | — |

**FEED tile** content: one pill per output (max 3 shown, 4 -> `w = 24`), `h = 14`, at
`y = ty+14`, `w = 33`, 3 px apart, starting `x = tx+6`. Clean feed on: `fillRoundRect` r3 `OK`,
legend `BG`. Off: `drawRoundRect` r3 `LINE`, legend `TEXT_2`. Unknown: `TEXT_3` legend. Legend
`Font0` centred: `SDI`, `HDMI`, `USB`. The selected output (when the tile is selected) has an
`ACCENT` 1 px underline `(px, ty+30, w, 1)`. No displays: `--` like other values.

### 4.3 External change highlight

When a value changes from the camera/phone while no local write for it is pending, the tile
outline flashes `INFO` (1 px `drawRoundRect`) fading to the normal outline over 600 ms
(`blend565`). If the tile is selected, the amber frame stays and the value text flashes `INFO` ->
`TEXT` over 400 ms instead. This makes phone-app changes visible without stealing attention.

---

## 5. Detail ("adjust") views

### 5.1 When they open and close

View state: `Overview | Detail(row) | Help | Intro(card)`. Connection screens take precedence
whenever `phase != Connected`.

Opens (`Detail(selected row)`):
- `◄` / `►` on the overview: opens **and applies the step** (no wasted press).
- `Enter` on any row (FOCUS: AF, WB: auto WB, FEED: toggle selected output; IRIS/ISO/TINT: just
  open).
- `Space` / `G0` short press: selects FOCUS, opens it, starts AF.
- Letter jump `f i o w t c`: selects and opens that control (no value change).

Closes (back to Overview, selection kept):
- `▲` / `▼`: closes and moves the selection in the same press (navigation lives in the
  overview).
- `Backspace` (`del`) or `` ` `` (`esc`): closes.
- Idle: **3000 ms** after the last key event, measured only when no key is held, no AF / auto WB
  is running and no value for this control is pending. While a key is held the view never closes.

Transition (open): an `ACCENT` 2 px round rect animates from the tile rect to `(2, 15, 236, 105)`
over ~120 ms (`approach`, half-life 30 ms); the detail content is drawn from the first frame
underneath it. Close: the reverse rectangle shrinks into the tile on the overview. No other
screen transitions.

### 5.2 Common detail frame

```
+--------------------------------------------------------------------------+
| status bar                                                               |  y 0..13
| FOCUS                                              [chip] [chip]         |  y 16..31 title row
|                                                                          |
|  hero value (24 pt)                    control graphic                   |  y 32..69
|                                                                          |
|  scale / tape / drum                                                     |  y 70..117
| hint bar                                                                 |  y 121..132
+--------------------------------------------------------------------------+
```

- Title: `Font2` `ACCENT`, top-left `(6, 16)`.
- Chips: right-aligned at `x = 234`, `y = 18`, 4 px apart, right to left.
- **Step-size chip**: while Shift is held or the auto-repeat has switched to big steps,
  show `x10` / `x3` / `1 STOP` / `500K` (per control) as an `ACCENT` filled chip. Explains why the
  value jumps.
- Hero baseline at `y = 66`, left at `x = 6`, unless stated.
- Main area `y 32..118`; hint bar as in 3.3.

### 5.3 FOCUS

```
+--------------------------------------------------------------------------+
| FOCUS                                                         [ MF ]     |
|                                                 +------------------+     |
|  69.5 %                                         |  |_          _|  |     |
|  (amber while pending, white confirmed)         |       +          |     |
|                                                 |  |-          -|  |     |
|                                                 +------------------+     |
|                         v (amber index)                                  |
| |    ,    |    ,    |    ,    |    ,    |    ,    |    ,    |    ,       |
| |         |         |         |         |         |         |            |
| 40       50        60        70        80        90       100 /\/\       |
|  [=========================|======[      ]==========]  minimap           |
| [, <][/ >] focus  [SHIFT] x10  [SPC] AF  [; ^][. v]                      |
+--------------------------------------------------------------------------+
```

- **Hero**: `69.5` `FreeSansBold24pt7b` baseline-left `(6, 66)`, then `%` `FreeSansBold12pt7b`
  at `(heroRight + 2, 66)`.
- **AF viewfinder** `(160, 30, 72, 40)`: `drawRoundRect` r3 `LINE`. Centre `(196, 50)`. Centre
  cross 5 px `TEXT_3`. AF frame = four L-corners, arm 6 px, thickness 2 px (`fillRect`), half-size
  `s`:
  - MF idle: `s = 10`, `TEXT_3`.
  - AF running: `s` animates 22 -> 9 over 400 ms then restarts (sawtooth), `INFO`; the viewfinder
    outline is `INFO`.
  - AF done: `s = 9` snapped, `OK`, for 1200 ms, then fades to idle over 300 ms.
  - AF timeout (no focus value 2000 ms after the command): `s = 9`, `TEXT`, chip `AF DONE`
    (neutral; the camera may simply already be in focus).
- **Focus tape** `(3, 76, 234, 35)` fill `PANEL`, clipped (`setClipRect`) to that rect:
  - Scale: 1 % = 6 px; the displayed position `vDisp` (0..100, eased toward the commanded value,
    half-life 35 ms) sits under the index at `x = 120`. For each integer `p` with
    `|p - vDisp| <= 20`: `x = 120 + (p - vDisp) * 6`.
  - Ticks from the tape top `y = 76`: `p % 10 == 0` -> 2 px wide, 15 px tall, `TEXT`, label
    `Font0` `TEXT_2` centred top at `(x, 95)` (`0`..`100`); `p % 5 == 0` -> 1 px, 10 px, `TEXT_2`;
    others -> 1 px, 5 px, `LINE`.
  - Hard stops: beyond 0 and 100 fill with diagonal hatch (`drawLine` every 6 px, `LINE`); a 3 px
    end bar `TEXT_2` at `p = 0` and `p = 100`. Near glyph at `p = 0` instead of the label: person
    (head `fillCircle r3`, body `fillRoundRect 8x6 r2`, `TEXT_2`). Far glyph at `p = 100`: two
    overlapping `fillTriangle` mountains (14 x 8), `TEXT_2`.
  - **Index** (commanded): `fillTriangle (114,70)(126,70)(120,78)` `ACCENT` and a 2 px line
    `fillRect(119, 76, 2, 35, ACCENT)`.
  - **Actual** (camera-reported) marker: `fillTriangle` pointing up from the tape bottom at
    `x = 120 + (focusConfirmed*100 - vDisp) * 6`, base y 110, apex y 103, `TEXT`. Drawn only when
    it differs from the index by >= 2 px. This is how "optimistic vs confirmed" looks on focus.
- **Minimap** `(6, 114, 228, 4)`: track `LINE`; fill `TEXT_3` from 0 to the value; the visible
  window (value +-20 %) as a 1 px `TEXT_2` outline; current position 2 px `ACCENT` tick 6 px tall
  (`y 113..118`).
- Chips: `MF` (TEXT_2 outline) / `AF` (INFO filled, blinking 250 ms) / `AF OK` (OK filled, 1200 ms) /
  `AF DONE` / padlock + `LOCKED` (WARN outline) when not controllable.
- `Enter` and `Space` both run AF here. A second AF while one is running is ignored (the `AF` chip
  flashes once).

### 5.4 IRIS

```
+--------------------------------------------------------------------------+
| IRIS                                                 [CLOSE 2]           |
|                                                      .-------.           |
|  f/ 2.8                                             / \ _ _ / \          |
|                                                    |  (light) |          |
|                                                     \ / - - \ /         |
|                                                      '-------'           |
|                    v                                                     |
|  /////[==========================================]///////////            |
|  1    1.4   2    2.8   4    5.6   8    11   16   22                      |
| [, <] open  [/ >] close  [SHIFT] x3  [; ^][. v]                          |
+--------------------------------------------------------------------------+
```

- **Hero**: `f/` `FreeSansBold12pt7b` baseline `(6, 66)`, then `2.8` `FreeSansBold24pt7b`
  directly after (+1 px). Formatting: one decimal, trailing `.0` removed (`f/4`, `f/16.4`).
- **Pending steps chip** right of the hero at `(heroRight + 8, 42)`: `ACCENT` outline chip with a
  5 px triangle (left = opening, right = closing) and the queued step count, e.g. `> 2`. Iris
  is stepwise and the target stop is unknown until the motor settles, so the hero keeps the last
  confirmed value (white) and the chip carries the pending intent. On confirmation the chip
  disappears and the hero flashes (6.1).
- **Aperture** centred `(194, 60)`:
  - Housing `fillCircle(194, 60, 28, BLADE)`, rim `drawCircle r28` and `r27` in `LINE`.
  - Opening radius `r = 3 + 21 * (irisMin / irisDisp)` (wide open = 24 px, f/16 ~ 5 px), where
    `irisDisp` eases to the confirmed stop (half-life 60 ms).
  - 7 vertices `θk = rot + k * 2π/7`, `rot = 0.9 * (1 - r/24)` rad (blades rotate as they close).
    Fill the opening as 7 triangles from the centre in `LIGHT`.
  - Blade seams: for each vertex draw a line from the vertex to the rim point at `θk + 1.2 rad`
    (radius 27), `LINE`; while steps are pending these seams are `ACCENT`.
  - AE active: opening in `TEXT_3` instead of `LIGHT`, and an `AUTO` INFO chip on the title row.
- **f-stop scale** `x 10..229`, axis at `y = 104`:
  - Position: `x = 10 + 2*log2(f) * 24.7` (f/1 at x 10, f/22 at x 230).
  - Full stops 1, 1.4, 2, 2.8, 4, 5.6, 8, 11, 16, 22: tick 1 px, 6 px up from the axis, `TEXT_2`;
    labels `Font0` `TEXT_2` centred top at `(x, 108)`. Third stops: 3 px ticks `LINE`.
  - **Lens range** `irisMin..irisMax`: bar `fillRect(x(min), 101, x(max) - x(min), 4, TEXT_2)`.
    Outside the range: hatched (`LINE`, every 4 px) from y 101 to 104 so the lens limits are
    obvious. Small 1 px end caps 8 px tall in `TEXT` at both limits.
  - **Marker** at `x(irisDisp)`: marker triangle apex `(x, 99)`, base y 91, `TEXT`. While steps are
    pending: one `ACCENT` 5 px chevron per queued step (max 3) beside the marker in the pending
    direction, 6 px apart.
  - At a limit: pressing further flashes the end cap `WARN` for 300 ms (no command sent if the model
    knows it is at the limit).

### 5.5 ISO

```
+--------------------------------------------------------------------------+
| ISO                                                     [+2/3 EV]        |
|                    +----------------------+                              |
|     640            |        800           |           1000               |
|                    +----------------------+                              |
|  ' ' | ' ' | ' ' | ' ' | ' ' |^' ' | ' ' | ' ' | ' ' | ' ' | ' ' | '     |
|  100        400        1.6K        6.4K        25.6K                     |
| [, <][/ >] ISO  [SHIFT] 1 stop  [; ^][. v] back                          |
+--------------------------------------------------------------------------+
```

- **Drum**: centre window `fillRoundRect(64, 36, 112, 44, 5, PANEL_HI)`, outline 2 px `ACCENT`.
  Items are the supported ISO list, pitch 92 px, positioned at
  `x = 120 + (i - idxDisp) * 92` where `idxDisp` eases to the selected index (half-life 45 ms).
  Items with `|x - 120| < 160` are drawn, `middle_center` at `y = 58`:
  - the selected index: `FreeSansBold18pt7b`, `TEXT` (or `ACCENT` while pending);
  - others: `FreeSans9pt7b`, colour `blend565(TEXT_2, TEXT_3, |x-120|/92)`.
  - Clip the drum to `(3, 34, 234, 48)`.
- **Ruler** `x 12..228`, base `y = 100`: one tick per supported ISO at
  `x = 12 + i * 216/(count-1)`; full stops (every 3rd value from 100) 8 px `TEXT_2`, others 4 px
  `LINE`. Labels `Font0` `TEXT_2` top `y = 104` under 100, 400, 1600, 6400, 25600 written as
  `100`, `400`, `1.6K`, `6.4K`, `25.6K` (labels at the right edge are `top_right`-aligned so they
  do not overflow). Marker above the axis: triangle apex `(x(idxDisp), 99)`, base y 91, `ACCENT`.
- **EV chip**: exposure change since this detail view opened, in thirds:
  `+1/3 EV`, `+2/3 EV`, `+1 EV`, `-1 1/3 EV` ... `TEXT_2` outline. Hidden at 0. (ISO steps are
  1/3 stop, so this is `indexDelta/3`.) Useful when compensating iris with ISO.
- At the ends of the list: the window outline flashes `WARN` 300 ms.

### 5.6 WHITE BALANCE

```
+--------------------------------------------------------------------------+
| WHITE BALANCE                                         [DAYLIGHT]         |
|                                                         \ | /            |
|  5600 K                                                - (O) -           |
|                                                         / | \            |
|                                                        DAYLIGHT          |
|                                      v                                   |
|  [orange ======================= white ================== blue ]        |
|  2500                                                     10000          |
|  G [green ------------------|------------------ magenta] M               |
| [, <][/ >] kelvin  [SHIFT] 500  [ENT] auto WB                            |
+--------------------------------------------------------------------------+
```

- **Hero**: `5600` `FreeSansBold24pt7b` baseline `(6, 66)`, `K` `FreeSansBold12pt7b` after it.
- **Preset icon** centred `(200, 46)`, ~30 px, drawn in `kelvinLut` colour of the preset when the
  value is within 150 K of it, else a neutral `TEXT_3` thermometer (rounded rect 6 x 22 + circle r5).
  Label `Font0` `TEXT_2` centred top at `(200, 64)`:
  - 3200 `TUNGSTEN`: bulb = `fillCircle(200, 42, 10)` + base `fillRect(195, 52, 10, 6)` with two
    `LINE` lines across.
  - 4000 `FLUORESCENT`: `fillRoundRect(184, 40, 32, 9, 4)` + pins `fillRect` 2 x 3 at each end.
  - 5600 `DAYLIGHT`: sun = `fillCircle(200, 46, 8)` + 8 rays (`drawWideLine`, width 2, from r11 to
    r16).
  - 6500 `CLOUDY`: three overlapping circles r7, r9, r6 at (191,49) (200,44) (209,50) + `fillRect`
    base.
  - 7500 `SHADE`: house: `fillTriangle` roof (184,46)(216,46)(200,32) + `fillRect(188,46,24,14)`.
- **Kelvin bar** `(10, 82, 220, 14)`: 220 vertical lines from `kelvinLut`. Marker at
  `x = 10 + (wbDisp - 2500) * 219/7500` (`wbDisp` eased, half-life 50 ms): triangle `TEXT` apex
  `(x, 81)` base y 73, plus a 3 px `BG` line and 1 px `TEXT` line through the bar (`y 80..97`).
  Preset ticks under the bar at 3200/4000/5600/6500/7500: 1 x 3 px `TEXT_3` at y 97..99.
  End labels `Font0` `TEXT_3`: `2500` top-left `(10, 99)`, `10000` top-right `(229, 99)`.
- **Tint context bar** `(22, 111, 196, 5)`: `tintLut` sampled, dimmed 50 % (`blend565` with `BG`),
  centre tick `TEXT_3`, current tick 2 px `TEXT`. `G` at `(10, 110)` and `M` at `(224, 110)`,
  `Font0` `TEXT_3`.
- **Auto WB** (`Enter`): chip `AUTO WB` `INFO` filled, blinking; the hero dims to `TEXT_3`; a
  scanning band (two 1 px `TEXT` lines 10 px apart) sweeps the Kelvin bar left to right, 700 ms per
  pass, repeating. When a new WB value arrives (event or re-read) the marker eases to it, the hero
  returns to `TEXT` with the confirm flash, chip `AUTO WB OK` (`OK`) for 1200 ms. Timeout 3000 ms ->
  chip disappears, banner `NO REPLY FROM CAMERA` (warn) only if the request itself failed;
  otherwise silent (same value).

### 5.7 TINT

```
+--------------------------------------------------------------------------+
| TINT                                                   [MAGENTA]         |
|                                                     +-------------+      |
|  +12                                                | grey card   |      |
|                                                     | (tinted)    |      |
|                                                     +-------------+      |
|                                      v                                   |
|  [green ====================== grey | ================== magenta]        |
|  -50 G                             0                        M +50        |
|  [kelvin context bar, dim]                                               |
| [, <] green  [/ >] magenta  [SHIFT] x10                                  |
+--------------------------------------------------------------------------+
```

- **Hero**: `+12` / `0` / `-7` `FreeSansBold24pt7b` baseline `(6, 66)`.
- **Grey-card swatch** `fillRoundRect(164, 30, 66, 36, 4, c)` where `c` = (128,128,128) shifted by
  tint: `t = tint/50`; `t > 0`: R +60t, G -60t, B +50t; `t < 0`: G +60|t|, R -40|t|, B -40|t|.
  1 px `LINE` outline. Label `Font0` `TEXT_3` centred top at `(197, 68)`: `GREY CARD`.
- **Tint bar** `(10, 82, 220, 16)` from `tintLut`. Centre detent: 2 px `TEXT` line `y 78..101` at
  `x = 120`. Labels `Font0` `TEXT_2` top at y 102: `-50 G` left (`x = 10`), `0` centred at 120,
  `M +50` right-aligned at 229. Marker like WB (triangle apex `(x, 81)`, `ACCENT` while pending,
  `TEXT` confirmed; `BG`/`TEXT` line through the bar).
- **Detent feedback**: when the commanded tint lands on or crosses 0, the centre line flashes `OK`
  for 250 ms (camera-style click).
- Chip: `GREEN` (`TINT_G` outline) for < 0, `NEUTRAL` (`TEXT_2` outline) for 0, `MAGENTA`
  (`TINT_M` outline) for > 0.
- **Kelvin context bar** `(10, 113, 220, 4)`: `kelvinLut` dimmed 50 %, current tick 2 px `TEXT`.

### 5.8 CLEAN FEED

```
+--------------------------------------------------------------------------+
| CLEAN FEED                                                               |
|  +================+   +----------------+   +----------------+            |
|  | [mini monitor] |   | [mini monitor] |   | [mini monitor] |            |
|  |  overlay bars  |   |  clean picture |   |  overlay bars  |            |
|  |      SDI       |   |      HDMI      |   |      USB       |            |
|  |  [ OVERLAY ]   |   |  [  CLEAN  ]   |   |  [ OVERLAY ]   |            |
|  +================+   +----------------+   +----------------+            |
| [, <][/ >] output  [ENT] clean feed on/off                               |
+--------------------------------------------------------------------------+
```

- Tiles: `n` outputs; `tw = 110` for n <= 2, `74` for 3, `56` for 4; gap 6; total
  `T = n*tw + (n-1)*6`; first `x0 = (240 - T)/2`; `y = 32`, `h = 86`.
- Tile frame: `fillRoundRect r4 PANEL`; selected: 2 px `ACCENT` outline; else 1 px `LINE`.
- **Mini monitor** `(tx+6, ty+6, tw-12, (tw-12)*9/16)` (62 x 34 for 3 outputs): sky `SKY` top 60 %,
  ground `GROUND` below, sun `fillCircle r4 SUN` near the top-right, and **you** in the middle
  (head `fillCircle r4 SKIN`, body `fillRoundRect 12 x 10 r3` in `TEXT_3`). Clip to the monitor.
  - Overlay (clean feed OFF): white status bars: top row three `fillRect` 8 x 2 `TEXT` at
    `y+3`, bottom row two 10 x 2 bars + one 4 x 2 `ERR` "rec dot" bar at `bottom-4`, and frame
    guide L-corners (arm 4 px) `TEXT_2` inset 4 px.
  - Clean: picture only.
  - Toggle animation: the overlay bars slide out (top bars up, bottom bars down) or in over 160 ms
    (`approach`, half-life 40 ms), clipped by the monitor rect.
- **Name** `Font2` `TEXT` centred at `(tx + tw/2, ty + 50)`: `SDI` / `HDMI` / `USB`.
- **State chip** `(tx+6, ty+66, tw-12, 14)` r3, `Font0` centred: `CLEAN` = `OK` fill, `BG` text;
  `OVERLAY` = `LINE` outline, `TEXT_2` text; unknown = `--` `TEXT_3`; pending = `ACCENT` fill,
  `BG` text showing the *target* state (`CLEAN` / `OVERLAY`).
- `◄►` moves the selection (the tile outline animates like the overview frame); `Enter` toggles
  the selected output.

---

## 6. Feedback states

### 6.1 Commanded vs confirmed

Per control the firmware keeps `pendingSince` (0 = none), the last confirmed value and
`confirmedAt`, `externalAt`, `failedAt`.

| State | Overview tile value | Detail hero / marker |
|---|---|---|
| Confirmed, idle | `TEXT` | `TEXT`, marker `TEXT` |
| Pending (sent, not yet reported back) | `ACCENT` | hero `ACCENT`; commanded marker `ACCENT`, actual marker `TEXT` (focus), steps chip (iris) |
| Just confirmed | `TEXT`; gauge flashes `OK` -> normal over 300 ms | `TEXT`; 2 px underline under the hero `OK` -> `BG` over 300 ms |
| Changed elsewhere | tile outline flash `INFO` 600 ms (4.3) | hero `INFO` -> `TEXT` 400 ms; marker eases to the new value |
| Pending > 1500 ms | `ACCENT`, and a 3-dot "waiting" (dots r1 at 4 px pitch, one lit, cycling 200 ms) after the value | same, after the hero |
| Pending > 3000 ms, no error | revert to the camera value, flash `WARN` 400 ms, banner `NO REPLY FROM CAMERA` | same |

Confirmation rule: a reported value equal to (or, for focus, within 0.01 of) the commanded value
clears pending. For iris, any reported stop after the last queued step clears pending.

### 6.2 Errors

- **Write failed** (HTTP error / timeout): value snaps back to the confirmed value; the value text
  (tile or hero) is drawn `ERR` and shakes horizontally (offset `+3, -3, +2, -2, 0` px over 5
  frames, ~170 ms); banner `WRITE FAILED` (error, 2500 ms).
- **403 not allowed**: same, banner `NOT ALLOWED (403)`, plus padlock on the tile for 2500 ms.

### 6.3 Operations in progress

- **AUTOFOCUS**: banner `AUTOFOCUS` (info), FOCUS tile badge `AF` (pulsing), focus detail as 5.3.
  Done = first focus value received >= 300 ms after the command, else 2000 ms timeout.
- **AUTO WB**: banner `AUTO WHITE BALANCE` (info), WB detail as 5.6.

### 6.4 Unavailable controls

Padlock glyph (9 x 10): shackle `drawArc(cx, cy, 3, 2, 180, 360)` above a `fillRoundRect` 7 x 5 r1.

| Condition | Tile | Detail | Key press on it |
|---|---|---|---|
| Focus not controllable | value `TEXT_3`, padlock badge | tape and hero `TEXT_3`, chip `LOCKED`; hint bar shows `LENS FOCUS NOT CONTROLLABLE` in `WARN` instead of keys | value shakes, banner `LENS NOT CONTROLLABLE` (warn); AF still sent (camera decides) |
| Iris under auto exposure | value `TEXT_2`, `AUTO` INFO badge | aperture in `TEXT_3`, chip `AUTO`; hint bar `IRIS SET BY AUTO EXPOSURE` (`WARN`) | shake, banner `IRIS ON AUTO EXPOSURE` |
| Iris not controllable / `irisStop == 0` | `--`, `NO LENS` badge | scale drawn without marker, chip `NO LENS` | shake, banner `LENS NOT CONTROLLABLE` |
| Value unknown | `--` `TEXT_3`, gauge hidden | hero `--`, no marker | nothing sent |

### 6.5 Live link down

Status bar `POLL` (3.1) + one-time warn banner `LIVE LINK LOST - POLLING`. Values still update
(every 2 s); external-change flashes still fire. When events resume: `OK` banner
`LIVE LINK RESTORED` once.

---

## 7. Non-control screens

### 7.1 Connection (`phase != Connected`)

Status bar variant: left `CamCtrl` in `TEXT`, right phones + `[h]` (no link indicator).

```
+--------------------------------------------------------------------------+
| CamCtrl                                                   []1  [h]       |
|      (v)----------------(o)----------------( )                           |
|      USB              NETWORK              CAMERA                        |
|                 PLUG INTO THE CAMERA                                     |
|           USB cable into the camera's USB-C port                         |
| +----------------------------------------------------------------------+ |
| | WIFI   EthAir-1A2B                                                   | |
| | PASS   ********                                                      | |
| | WEB    http://192.168.73.1                                           | |
| +----------------------------------------------------------------------+ |
+--------------------------------------------------------------------------+
```

- **Step nodes** centres `(40, 24)`, `(120, 24)`, `(200, 24)`, r 8; connecting lines
  `drawWideLine` width 2 between node edges: `OK` between done nodes, `LINE` otherwise.
  - done: `fillCircle OK` + check mark (`drawWideLine` width 2 `BG`: `(-4,0)->(-1,3)->(4,-3)`
    relative).
  - current: ring `drawCircle` r8 and r7 `ACCENT`, inner dot r3 `ACCENT`, plus a breathing outer
    ring `r = 9 + 2*sin(t*2π/1000ms)` in `ACCENT_DIM`.
  - future: `drawCircle r8 TEXT_3`.
  - Labels `Font0` `TEXT_2` centred top at `(x, 36)`: `USB`, `NETWORK`, `CAMERA`.
  - NoUsb: current = USB. WaitingIp: USB done, current = NETWORK. Connecting: USB, NETWORK done,
    current = CAMERA.
- **Title** `FreeSansBold9pt7b` `ACCENT` centred, baseline `(120, 61)`:
  `PLUG INTO THE CAMERA` / `WAITING FOR CAMERA` / `CONNECTING...` (the dots animate 1-3, 400 ms).
- **Subtitle** `Font2` `TEXT` centred top `(120, 65)`: `USB cable into the camera` /
  `Set camera network to DHCP` / `Talking to the camera`.
- **Wi-Fi panel** `fillRoundRect(4, 84, 232, 36, 4, PANEL)`: three lines, `Font0` labels
  `TEXT_2` at `x = 10`, `Font0` values `TEXT` at `x = 50`, line tops at `y = 88, 100, 112`:
  `WIFI` ssid, `PASS` password, `WEB` `http://192.168.73.1` (`INFO`). (Font0 keeps all three lines
  in 36 px; the help screen repeats them.)
- **Hint bar**: `[h] keys + Wi-Fi`.
- **Power-switch warning** (NoUsb for > 15 s): the subtitle and Wi-Fi panel are replaced by a red
  card `fillRoundRect(4, 66, 232, 54, 5, ERR)`:
  - switch graphic at `(14, 80)`: `fillRoundRect(14, 80, 34, 16, 8, BG)` slot, knob
    `fillCircle(22, 88, 6, TEXT)` on the left, label `OFF` `Font0` `TEXT` under the left end
    `(14, 99)` and `ON` dimmed `(40, 99)`; an animated arrow (`fillTriangle`) pointing left that
    nudges 2 px every 500 ms.
  - text `Font2` `TEXT` top-left at `(60, 70)` and `(60, 86)`: `Set the CARDPUTER` /
    `power switch OFF`; `Font0` `TEXT` at `(60, 106)`: `Battery on = camera USB stays off`.
  - The title stays `PLUG INTO THE CAMERA`.

### 7.2 Help (`h` from any screen; any key closes, except `Enter`)

```
+--------------------------------------------------------------------------+
| KEYS                                                 any key: close      |
| [; ^][. v]        choose control                                         |
| [, <][/ >]        change value - hold = fast                             |
| [SHIFT]+[, <]     big step                                               |
| [SPACE] [G0]      AUTOFOCUS                                              |
| [ENTER]           AF / auto WB / feed on-off                             |
| [f][i][o][w][t][c] jump: focus iris iso wb tint feed                     |
| [DEL]             back to overview        [ENTER] replay intro           |
| WIFI EthAir-1A2B  PASS ********   http://192.168.73.1                    |
+--------------------------------------------------------------------------+
```

- Full screen, `fillScreen BG`. Title row `(0, 0, 240, 14)` `STATUS_BG`: `KEYS` `Font0` `ACCENT`
  at `(4, 3)`; `any key: close` `Font0` `TEXT_2` right-aligned at `(236, 3)`.
- Seven rows, pitch 14, first cap at `y = 17` (rows at y 17, 31, 45, 59, 73, 87, 101). Caps start
  at `x = 6`; description `Font0` `TEXT` at `x = 84` (row 6 description at `x = 6 + capsWidth + 4`
  because six caps are 84 px wide). `AUTOFOCUS` in `INFO`.
  Row 7 right side: `[ENTER] replay intro` starting `x = 140`.
- Footer `(0, 116, 240, 17)` `PANEL`: `Font0` `TEXT_2` at `(4, 117)`: `WIFI <ssid>  PASS <pw>`;
  at `(4, 126)`: `http://192.168.73.1` (`INFO`) and right-aligned `phones: <n>`.
- `Enter` in help starts the intro (7.3) and resets hint level to FULL (8).

### 7.3 First-connect intro (onboarding)

Shown once, the first time `phase` becomes `Connected` and NVS flag `introSeen` is false. Two
cards. Replayed from help with `Enter`. Values keep updating underneath (not drawn).

Card 1 - autofocus (teach by doing):

```
+--------------------------------------------------------------------------+
| WELCOME                                                       1 / 2      |
|                ONE-PRESS AUTOFOCUS                                       |
|      +--------------------------+        +--------+                      |
|      |          SPACE           |   or   |   G0   |  side button         |
|      +--------------------------+        +--------+                      |
|           focuses on the centre of the picture                           |
|                  try it now: press SPACE                                 |
|                         o  .                                             |
| any other key: next                                                      |
+--------------------------------------------------------------------------+
```

- Title row like help: `WELCOME` `ACCENT`, `1 / 2` `TEXT_2` right.
- Heading `FreeSansBold9pt7b` `TEXT` centred baseline `(120, 34)`.
- Big caps (cap style scaled up, `Font2` legend, r5, 2 px `LINE` border, 2 px depth line):
  `SPACE` `(20, 44, 110, 26)`, `or` `Font2` `TEXT_2` at `(140, 50)`, `G0` `(166, 44, 46, 26)` with
  `side button` `Font0` `TEXT_2` centred under it at `(189, 73)`.
- `Font2` `TEXT_2` centred top `(120, 80)`: `focuses on the centre of the picture`;
  `Font0` `ACCENT` centred top `(120, 98)`: `try it now: press SPACE` (blinking 600 ms).
- Page dots at `(116, 110)`, `(124, 110)` r2: current `ACCENT`, other `LINE`.
- `Space`/`G0`: really runs AF; the SPACE cap fills `INFO`, then `OK` with a check when AF is done,
  and the card advances after 800 ms. Any other key: next card (key not executed).

Card 2 - navigating (the inverted T matches the physical key positions):

```
+--------------------------------------------------------------------------+
| WELCOME                                                       2 / 2      |
|            +----+          ^ v   choose a control                        |
|            | ;^ |          < >   change it                               |
|      +----++----++----+          hold = fast, SHIFT = big step           |
|      | ,< || .v || /> |    [ENTER] AF / auto WB / feed on-off            |
|      +----++----++----+    [f i o w t c] jump to a control               |
|                            [h] all keys, any time                        |
|                         .  o                                             |
| any key: start                                                           |
+--------------------------------------------------------------------------+
```

- Caps 30 x 22 r4: `;` up `(44, 22)`; `,` left `(10, 48)`; `.` down `(44, 48)`; `/` right
  `(78, 48)`. Each shows a 9 px `ACCENT` triangle centred and its legend char `Font0` `TEXT_2` in
  the bottom-right corner (`x+22, y+12`).
- Right column `x = 118`, `Font0`: rows at y 24, 38, 52, 68, 84, 100. Arrow pairs are drawn as
  5 px triangles; captions `TEXT`; `hold = fast, SHIFT = big step` `TEXT_2`; small key caps
  inline.
- Any key: closes, sets `introSeen = true` (one NVS write), returns to the overview with FOCUS
  selected. The key is not executed.

### 7.4 Factory reset countdown (G0 held)

Visible from 1000 ms of holding (shorter holds are autofocus, see 9.2). Full-screen overlay drawn
last, on every screen including safe mode.

```
+--------------------------------------------------------------------------+
|                         FACTORY RESET                                    |
|                         .-''''-.                                         |
|                        /   3    \     (red arc fills clockwise)          |
|                        \        /                                        |
|                         '-....-'                                         |
|             all settings erased, Wi-Fi password reset                    |
|                    release G0 to cancel                                  |
+--------------------------------------------------------------------------+
```

- `fillScreen BG`, 2 px `ERR` border.
- `FACTORY RESET` `FreeSansBold9pt7b` `ERR` centred baseline `(120, 20)`.
- Ring centre `(120, 62)`, inner radius 30, outer 36: track `fillArc(120, 62, 30, 36, 0, 360, LINE)`;
  progress `fillArc(120, 62, 30, 36, 270, 270 + 360*p, ERR)` with `p = (held - 1000)/4000`,
  i.e. clockwise from 12 o'clock (`fillArc` angle 0 = 3 o'clock; if the end angle exceeds 360,
  draw it as two arcs `270..360` and `0..end-360`). `drawSmoothArc` may replace `fillArc` for
  anti-aliased edges if available; mind its different angle origin.
- Seconds left (`ceil`), `Font7` `ERR`, `middle_center` at `(120, 62)`: `4`, `3`, `2`, `1`.
- `Font0` `TEXT_2` centred top `(120, 104)`: `all settings erased`; `Font2` `TEXT` centred top
  `(120, 115)`: `release G0 to cancel`.
- At 5000 ms: `fillScreen ERR`, `RESETTING` `FreeSansBold12pt7b` `TEXT` centred, then reboot.
- Released between 1000 and 5000 ms: overlay disappears, banner `RESET CANCELLED` (warn).

### 7.5 Safe mode

```
+--------------------------------------------------------------------------+
|/////////////////// amber/black hazard stripes ///////////////////////////|
|                           SAFE MODE                                      |
|              USB is a serial port for flashing.                          |
|                 Network and remote are off.                              |
|   [==============================                ]                       |
|                    normal mode in 2:41                                   |
+--------------------------------------------------------------------------+
```

- Hazard band `(0, 0, 240, 16)`: `ACCENT` background, black parallelograms (two `fillTriangle`
  each) 8 px wide every 16 px, slanted 45 deg.
- `SAFE MODE` `FreeSansBold12pt7b` `ACCENT` centred baseline `(120, 44)`.
- `Font2` `TEXT` centred tops `(120, 54)` and `(120, 72)`.
- Countdown bar `(20, 96, 200, 6)` r3: track `PANEL`, fill `ACCENT` shrinking from full to empty
  over `SAFE_MODE_TIMEOUT_MS`. `Font2` `TEXT_2` centred top `(120, 108)`: `normal mode in m:ss`.
- Factory reset overlay as 7.4.

---

## 8. Discoverability: contextual hint bar

The hint bar (3.3) is **always visible** on the overview, every detail view and the connection
screens. It has two levels; the layout (y 121..132) is identical so nothing jumps.

### 8.1 FULL hints (new users)

Cap notation: `[;^]` = cap `;` with an up arrow, etc.

| Screen | Hint set (left to right) | Width |
|---|---|---|
| Overview, FOCUS | `[;^][.v] pick` `[,<][/>] adjust` `[SPC] AF` | 197 |
| Overview, IRIS / ISO / TINT | `[;^][.v] pick` `[,<][/>] adjust` `[h] all keys` | 221 |
| Overview, WB | `[;^][.v] pick` `[,<][/>] adjust` `[ENT] auto WB` | 227 |
| Overview, FEED | `[;^][.v] pick` `[,<][/>] output` `[ENT] on/off` | 221 |
| Detail FOCUS | `[,<][/>] focus` `[SHIFT] x10` `[SPC] AF` `[;^][.v]` | 227 |
| Detail IRIS | `[,<] open` `[/>] close` `[SHIFT] x3` `[;^][.v]` | 207 |
| Detail ISO | `[,<][/>] ISO` `[SHIFT] 1 stop` `[;^][.v] back` | 215 |
| Detail WB | `[,<][/>] kelvin` `[SHIFT] 500` `[ENT] auto WB` | 217 |
| Detail TINT | `[,<] green` `[/>] magenta` `[SHIFT] x10` | 185 |
| Detail FEED | `[,<][/>] output` `[ENT] clean feed on/off` | 214 |
| Connection | `[h] keys + Wi-Fi` | 87 |

When the selected control is unavailable, the bar shows the reason instead (6.4), `Font0`
`WARN`, centred.

While a key is pressed, its cap in the hint bar is drawn "pressed" for 120 ms (fill `ACCENT`,
legend `BG`): the user sees the screen react to the very key they pressed and links key and
legend.

### 8.2 SLIM hints (learned)

Same positions, caps in the dim variant, words removed except the contextual action, e.g.
overview WB: `[;^][.v]` `[,<][/>]` `[ENT] auto WB` `[SPC] AF`; detail FOCUS: `[,<][/>]` `[SHIFT]`
`[SPC] AF`. Calmer, still a complete map of what works here.

### 8.3 When the level changes

- Starts FULL. Graduates to SLIM when, since boot, the user has made >= 3 navigations (`▲▼` or a
  letter), >= 5 adjustments that were confirmed by the camera, and >= 1 autofocus (any of
  Space / G0 / Enter). One NVS write (`hintsSlim = true`).
- Back to FULL for this session when the user seems lost: 2 presses of unmapped keys within 5 s
  (also shows banner `PRESS h FOR KEYS`, info). Any unmapped key in FULL mode also flashes the
  `[h]` cap in the status bar `ACCENT` for 400 ms.
- `Enter` in help (replay intro) resets to FULL permanently (clears `hintsSlim`).
- Factory reset clears both `introSeen` and `hintsSlim`.

### 8.4 Other discoverability details

- `[h]` cap in the status bar on every screen.
- Jump letters shown as mini caps on every overview tile (4.1).
- The first `◄►` press on the overview opens the detail *and* changes the value, so the user
  immediately sees what adjusting means.
- Fn is ignored for the arrow keys (Fn + `;` behaves like `;`), so users who think "arrows need
  Fn" are not punished.

---

## 9. Keymap and data refinements

### 9.1 Add tally and record (recommended)

Subscribe to `/camera/tallyStatus` and `/transports/0/record`. Self-shooters need "am I
recording / live" more than anything else on this screen, and it costs one pill and a border
(3.1, 3.2). Display only; no record key (out of scope, and a record toggle on a handheld is easy
to hit by accident).

### 9.2 Keys

All given keys keep working. Changes and additions:

| Key | Behaviour |
|---|---|
| `Space` | autofocus **on press** (instant) |
| `G0` | autofocus **on release** if held < 600 ms. 600..1000 ms: nothing. >= 1000 ms: reset countdown (7.4), cancel by releasing before 5 s. Avoids firing AF at the start of every reset attempt. |
| `Backspace` (`del`), `` ` `` (`esc`) | close detail / help / intro card. New. |
| `▲` / `▼` in a detail view | close it and move the selection (one press) |
| `Enter` on IRIS / ISO / TINT | open the detail view (previously nothing) |
| `h` | help from any screen including connection screens; `Enter` in help replays the intro |
| Fn + arrow keys | same as without Fn |
| AF while AF running | ignored (chip flashes) |
| `◄►` at a limit | no command; limit indicator flashes `WARN` |

Optional, not required: `1`..`3` toggle clean feed on output 1..3 from anywhere.

### 9.3 UI state the firmware needs (additions to `UiState`)

`view` (Overview / Detail / Help / Intro), `detailRow`, `lastKeyMs`, `keyHeld`, `bigStep`;
per control: `pendingSince`, `confirmedAt`, `externalAt`, `failedAt`, confirmed value; focus
`afState {Idle, Running, Done, Timeout}` + `afAt`; `autoWbState` + time; `irisQueuedSteps`
(signed); `isoIndexAtOpen`; `tally {None, Preview, Program}`, `recording`; `banner {kind, text,
at}`; `hintsSlim`, `introSeen`, `introCard`; `pressedCap` + time. Animated values (`vDisp`,
`irisDisp`, `idxDisp`, `wbDisp`, `tintDisp`, selection frame rect) live inside `ui.cpp`.

---

## 10. Rendering and frame budget

- Loop: render when the model changed **or** any animation is active (eased value not at target,
  flash/blink/banner/AF/auto-WB/pending-dots/countdown running). Animated: 30 fps (33 ms tick).
  Static: redraw at 2 fps only for blinks (status `POLL`), otherwise not at all.
- Per frame: `fillScreen` + content ~4-8 ms (gradients are 220 `drawFastVLine` calls; the tape
  draws <= 41 ticks; the drum <= 5 strings) + push ~13 ms -> ~20 ms, inside the 33 ms tick.
- `dt` for `approach()` comes from `millis()` between frames, so motion speed is independent of
  the frame rate.
- No extra buffers: LUTs 880 bytes; all icons procedural; all text from built-in fonts.
- Clip rects (`setClipRect` / `clearClipRect`) for the focus tape, ISO drum and feed monitors.
- Everything is drawn into the existing `s_fb` sprite; push once at the end of the frame.

## 11. Acceptance checklist (on hardware)

- At arm's length in daylight: every value, the selected tile and the tally border readable.
- A first-time user, given only the device, finds autofocus, selects ISO and changes it without
  help (intro + hint bar).
- Holding `►` on FOCUS: tape glides continuously, step chip shows `x10` after the boost.
- Changing ISO on the phone app: the ISO tile flashes `INFO` within one event.
- Iris: queued steps chip appears, disappears when the motor settles; lens limits visible.
- Auto WB: scan animation, marker eases to the new Kelvin, green confirm.
- Pulling the WebSocket (e.g. camera reboot): `POLL` + banner, values still update.
- G0 short press = AF; 5 s hold shows the countdown and resets; release at 3 s cancels.
