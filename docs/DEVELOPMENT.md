# Development notes

This document is for working on the firmware itself. It covers how the code is organized, what was
learned about the display, the serial debug console and the helper tools.

> **Language note**: code comments and log messages are in **Serbian** (Latin script); identifiers are
> in English. The user interface is bilingual (English / Serbian), see [Translations](#translations).

- [Architecture](#architecture)
- [Source map](#source-map)
- [Display pipeline and flicker](#display-pipeline-and-flicker)
- [Serial debug console](#serial-debug-console)
- [Tools](#tools)
- [Web app](#web-app)
- [Translations](#translations)
- [Storage](#storage)
- [Diagnostics](#diagnostics)
- [Testing tips](#testing-tips)

---

## Architecture

```
 core 1: Arduino loop task                         core 0: "cloud" task
 ┌────────────────────────────┐   command queue   ┌──────────────────────────────────┐
 │ LVGL (lv_timer_handler)    │ ────────────────▶ │ Wi-Fi, NTP                       │
 │ ui::update(), popups       │                   │ Tuya OpenAPI   (HMAC-SHA256 sign)│
 │ screen power (dim / off)   │                   │ eWeLink API v2 (OAuth2 tokens)   │
 │ debug console (Serial)     │                   │ polling: 10 s active / 5 min idle│
 └─────────────▲──────────────┘                   └───────────────┬──────────────────┘
               │ reads (revision counters)                        │ writes
               │        ┌──────────────────────────┐              │
               └─────── │ DeviceStore  (g_store)   │ ◀────────────┘
                        └──────────────────────────┘
                                     ▲ reads
                        ┌────────────┴─────────────┐
                        │ core 0: "web" task       │  port 80: phone web app + JSON API,
                        │ (WebServer)              │  eWeLink OAuth callback
                        └──────────────────────────┘
```

- **UI task**: the Arduino `loop()` on core 1. It runs LVGL, reads touch, calls `ui::update()`,
  manages the backlight and polls the debug console. It **never waits for the network**; it only puts
  requests on the cloud task's queue (`cloud::requestSet`, `requestCover`, `requestThermostat`,
  `requestAlarm`).
- **Cloud task**: `net/cloud_task.cpp`, core 0, priority 3. It runs Wi-Fi, NTP and polling for both
  clouds, and executes commands.
  - **Optimistic UI**: a command marks the device as `pending` with a short "hold", so the next
    refresh doesn't flip the tile back. The real state is confirmed ~4 s later
    (`CONFIRM_AFTER_CMD_MS`).
  - **Activity**: "active" means within 90 s after the last touch (`ACTIVE_WINDOW_MS`). Going from
    idle to active triggers an immediate refresh.
  - Devices **disabled** in the app are skipped everywhere: no commands, no spec/log queries, no
    alerts.
- **Web task**: `net/web_portal.cpp`, core 0, priority 2. It serves the single-page phone app and its
  JSON API. Anything that must run on the UI side, such as running a scene that may need the alarm
  PIN, is handed over as a request.
- **DeviceStore**: `model/device_store.*` holds the shared state (switches, sensors, covers,
  thermostats, door/motion sensors, smoke detectors, alarm hubs, cloud status) behind a mutex.
  - Every change bumps `revision()`; a change in the set of devices also bumps `layoutRevision()`.
  - The UI rebuilds only when a revision changes, and only the parts whose signature changed. This
    matters a lot for flicker (see below).
- **CloudProvider**: `net/cloud_provider.h` is the common interface for `TuyaClient` and
  `EWeLinkClient` (`fetch`, `setSwitch`, …). Each client owns one keep-alive `HttpsSession`, which
  avoids a ~1 s TLS handshake per command.

## Source map

| Path | What it is |
|---|---|
| `src/main.cpp` | `setup()` / `loop()`, screen power (awake → dim → off; stays awake during alarm/smoke), boot diagnostics |
| `src/board/display.*` | ESP-IDF RGB panel + LVGL display glue (DIRECT mode, double buffer, VSYNC), redraw log, live PCLK change |
| `src/board/touch_gt911.*` | Minimal GT911 I²C driver (polled, no INT pin) |
| `src/model/device_store.*` | Shared device state with revisions, optimistic command state |
| `src/model/settings.*` | NVS-backed settings: Wi-Fi, cloud keys, layout, pages, disabled devices, screen timeouts, alarm PIN hash, per-boot web PIN |
| `src/model/scenes.*` | Scenes (`/scenes.json` in LittleFS), max 16 × 32 actions |
| `src/model/*.h` | Device models and display helpers: `switch_info`, `cover`, `sensor`, `thermostat`, `binary_sensor`, `smoke`, `alarm`, `device_group` |
| `src/net/cloud_task.*` | Network task: Wi-Fi, NTP, polling, command queue, demo mode |
| `src/net/tuya_client.*` | Tuya OpenAPI: token, signing, device parsing, commands, specifications, DP logs |
| `src/net/ewelink_client.*` | eWeLink API v2: OAuth URL, code exchange, token refresh, devices, commands |
| `src/net/https_session.*` | Keep-alive HTTPS client with the ESP-IDF CA bundle |
| `src/net/web_portal.*` | Web server, PIN check and lockout, JSON API, photo upload, eWeLink OAuth endpoints |
| `src/net/web_app_html.h` | The whole phone web app (HTML + CSS + JS) as one raw string literal |
| `src/ui/ui.*` | Screen manager, device list (tiles by group), scene execution |
| `src/ui/home_screen.*` | Photo home screen: pages, markers, edit mode, clock, padlock |
| `src/ui/background.*` | Page photos: LittleFS / embedded image → PSRAM buffers, gentle flash reads |
| `src/ui/thermo_popup.*`, `cover_popup.*` | Pop-ups over any screen |
| `src/ui/alarm_screen.*`, `pin_prompt.*` | Alarm hub screen and PIN keypad |
| `src/ui/smoke_alert.*` | Full-screen smoke warning on `lv_layer_top` |
| `src/ui/settings_screen.*` | **Settings**: Wi-Fi, Tuya, eWeLink, Devices, Alarm, Web (QR, language) |
| `src/ui/theme.*`, `src/ui/fonts/` | Colours and TinyTTF Montserrat fonts (Serbian Latin + Cyrillic), generated with `tools/bin2c.py` |
| `src/ui/lv_mem_psram.c` | LVGL heap in PSRAM |
| `src/util/crypto.*` | SHA-256 / HMAC-SHA256 (hex, base64), URL encoding, random strings (mbedTLS) |
| `src/util/i18n.*` | Interface language and the `TR("srpski", "English")` macro |
| `src/util/restart.*` | Delayed restart (used to apply a language change) |
| `src/util/selftest.*` | Boot-time check of the signing code against known test vectors (`[selftest] SVE OK`) |
| `src/debug/console.*` | Serial debug console (below) |

## Display pipeline and flicker

| | |
|---|---|
| Panel | 800×480 RGB565 over a 16-bit parallel RGB bus (`esp_lcd_panel_rgb`) |
| Framebuffers | **two** full framebuffers in PSRAM (`num_fbs = 2`) |
| LVGL | 9.2.2 in `LV_DISPLAY_RENDER_MODE_DIRECT`. LVGL draws straight into the back buffer; when a frame is done the panel switches buffers on VSYNC, so there is no tearing. |
| Bounce buffer | `LCD_H_RES * 10` pixels in internal RAM. LCD DMA reads from it, and an ISR refills it from PSRAM. |
| Pixel clock | **14 MHz** (~34 Hz) instead of the stock 16 MHz, to reduce PSRAM pressure (`LCD_PCLK_HZ`; try live with `pclk N`) |
| Photos | each page photo is a 768 KB RGB565 buffer in PSRAM, drawn as an LVGL image |

The bounce buffer gives the ISR only about **0.3 ms of slack** at 14 MHz. When it can't refill the
buffer in time, you see **dashes, horizontal shifts or flicker**. Two causes were found and fixed;
keep them in mind when changing UI code.

### 1. Large redraws over the photo

Redrawing a big area over a PSRAM photo takes about 250 ms of heavy PSRAM traffic. Doing that
repeatedly (for example on every 10 s refresh) shows up as flicker. Common LVGL traps:

- `lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)` on an object that is **already visible** still
  invalidates it.
- `lv_obj_move_foreground()` invalidates the object even if it is already on top.
- Re-setting a label to the same text, or rebuilding a whole list when one value changed.

That's why the code uses guards like `setVisible()` (change only if different), `setText()` (compare
first) and per-section signatures in `ui.cpp`. Use the **`rf`** console command to log every redraw
with its pixel count, area count and duration. A full-screen redraw every 10 s means something is
invalidating too much.

### 2. Flash access with the cache disabled

While the SPI flash is read or written, the CPU cache is disabled. Code and data that are not in
internal RAM then stall, including the bounce-buffer refill. The Arduino-ESP32 build here does not use
`CONFIG_LCD_RGB_ISR_IRAM_SAFE`, so long flash operations starve the ISR.

- The Arduino `File` API reads through a 4 KB stdio buffer, which means 4 KB flash reads. That was
  enough to cause dashes while a photo loaded.
- **Fix** (`ui/background.cpp`): photos are read with POSIX `open()`/`read()` in **512 B** pieces
  (`readGently`), so each cache-off window stays short. File existence is cached (`stat` once). When
  the phone asks for a photo that is already in PSRAM, it is served from memory instead of flash.
- **Flash writes** (photo upload, saving layout or scenes, NVS) can still cause a brief glitch. This
  is accepted, because writes only happen when you change something.

## Serial debug console

The firmware reads simple commands on the USB serial port (115200 baud, one command per line). Replies
start with `@OK` / `@ERR`. The commands are meant for development and screenshots. Demo devices exist
**only in RAM** and are gone after a restart; saved settings, layout, scenes and photos are not
changed.

Every command except `s`/`s2` counts as a touch: the screen wakes up and does not turn off (turning off
returns to the home screen and would close a pop-up before the screenshot is taken).

### Screenshots

| Command | What it does |
|---|---|
| `s` / `s2` | Screenshot at full / half resolution. Includes the top layer (pop-ups, toasts): its visible children are moved onto the screen for the moment of the snapshot and then back, so no extra 1.5 MB ARGB buffer is needed. Sent as RLE + base64 lines `@H … @S <i> … @E`. Use `tools/screenshot.py`. |
| `r N` | Resend chunk N of the last screenshot (used by the tool to repair lines broken by other log output) |

### Demo data (RAM only, until restart)

| Command | What it does |
|---|---|
| `show` | **Showcase** for screenshots: replaces all devices and scenes with demo ones (lights, switches, sockets on both clouds, blinds, temperatures, thermostat, door, motion, smoke, alarm hub) and places markers on the built-in house image. Only one page is shown, with the built-in image even if a photo was uploaded. **Cloud polling stops** (results of a request already in flight are dropped), so no real device data appears on screen. Saved layout, scenes and photos are not touched; a restart brings everything back. |
| `show d` | Same as `show`, but the demo smoke detector reports smoke (red warning) |
| `d` | Demo switches (Tuya + eWeLink), demo mode on |
| `dc` | Demo blind |
| `ds` / `ds0` | Demo smoke detector with smoke / OK |
| `dt` | Demo thermostat, door and motion sensor, placed on the current page |
| `dv` / `dm` | Toggle the demo door / motion state |

All demo commands switch on demo mode, which stops cloud polling until the next restart.

### UI navigation

| Command | What it does |
|---|---|
| `h` | Back to the home screen (closes pop-ups, list, settings, alarm screen, edit mode) |
| `g` | Open the device list |
| `gs N` | Scroll the device list (used for screenshots of lower sections) |
| `e` | Home screen edit mode |
| `al KEY` | Open the alarm screen for alarm hub KEY (e.g. `al demo-a1` after `show`) |
| `lang en` / `lang sr` | Save the interface language and restart (same as the selector in settings) |
| `rst` | Restart the panel the same way a language change does (web PIN is kept) |
| `m` | Close settings / back to the main screen |
| `p N` | Open **Settings** on tab N without the PIN (0 Wi-Fi, 1 Tuya, 2 eWeLink, 3 Devices, 4 Alarm, 5 Web) |
| `th KEY` | Open the thermostat pop-up for a device key |
| `co KEY` | Open the blind pop-up for a device key |
| `t X Y` | Injected tap at X, Y |
| `w X1 Y1 X2 Y2` | Injected swipe (400 ms) |

> Injected touches act like real ones. On a panel connected to real devices, a tap can switch a real
> light. Use them only together with demo data.

### Diagnostics

| Command | What it does |
|---|---|
| `rf` | Toggle the redraw log: `[refr] N ms: pixels (%), areas` |
| `pclk N` | Set the pixel clock live, in MHz (6–20) |
| `w` | Print the web app URL including the current PIN (`@WEB http://…/?pin=…`) |
| `tp` | Run the Tuya parser on built-in sample responses (`wk`, `wkf`, `kt`, `wkcz`, `mcs`, `pir`, `hps`, `dj`), offline |
| `dl` | Print the last 7 days of smoke-detector DP history from the Tuya cloud |
| `dl <deviceId>` | Print 7 days of DP history of one device (skipped for disabled devices) |
| `ht` | Make a deliberately failing HTTPS request, to test connection recovery (the UI waits ~20 s) |

## Tools

All tools are in `tools/` and use the Python bundled with PlatformIO (`~/.platformio/penv/bin/python`),
which includes `pyserial`.

| Tool | Usage |
|---|---|
| `screenshot.py` | `screenshot.py out.png [--port P] [--cmd "show"] [--cmd "g"] [--wait 1.0] [--full]`: sends the commands, takes a screenshot and writes a PNG. Half resolution by default; `--full` for 800×480. |
| `serial_log.py` | `serial_log.py [port] [seconds]`: resets the board and prints the log with timestamps (non-interactive `pio device monitor`) |
| `make_background.py` | `make_background.py photo.jpg` → `assets/background.rgb565` (scale, center-crop, Floyd–Steinberg dithering to RGB565). `--placeholder` makes a gradient. Uses macOS `sips`. |
| `bin2c.py` | `bin2c.py in.ttf out.c array_name`: embeds a binary (the TTF font) as a C array |

## Web app

- The whole phone app is **one HTML file** embedded as a raw string literal in
  `src/net/web_app_html.h` (`R"HTML( … )HTML"`). There is no build step and no external
  resources, so it works without internet access.
- JSON API (every call needs `?pin=`):

  | Endpoint | Method | Purpose |
  |---|---|---|
  | `/api/state` | GET | everything the app needs: status, settings (no secrets), layout, pages, items, scenes |
  | `/api/layout` | POST | save layout markers |
  | `/api/hidden` | POST | enable / disable a device |
  | `/api/screen` | POST | dim / off timeouts |
  | `/api/tuya`, `/api/ewelink`, `/api/wifi` | POST | accounts and Wi-Fi |
  | `/api/bg?page=N` | GET / POST | download / upload a page photo (raw RGB565) |
  | `/api/bg/reset` | POST | back to the built-in photo |
  | `/api/pages` | POST | add / rename / delete pages |
  | `/api/scenes`, `/api/scenes/run` | POST | save / run scenes |
  | `/ewelink/start`, `/ewelink/login`, `/ewelink/callback` | GET | eWeLink OAuth flow |

- Items in `/api/state` have a type `t`:

  | `t` | Type |
  |---|---|
  | `s` | sensor |
  | `l` | switch / light |
  | `c` | cover |
  | `d` | smoke |
  | `x` | scene |
  | `a` | alarm |
  | `t` | thermostat |
  | `v` | door |
  | `m` | motion |

  Each item also has a source `c`: `t` Tuya, `e` eWeLink, `p` panel.

## Translations

Every user-visible string exists in both languages, written in place:

```cpp
#include "util/i18n.h"
lv_label_set_text(title, TR("Podešavanja", "Settings"));
```

- `TR(sr, en)` returns one of two string literals, so the pointer is always valid. Call it at runtime
  (after `settings::begin()`), never in a static initializer or a global array.
- The language is fixed for one boot. Screens are built once, so changing the language saves it to
  NVS and restarts the panel (`restart::soon()`); the web PIN is kept in RTC memory so the phone
  session survives.
- Decimal numbers use `i18n::decimalSep()` (`21,5°` in Serbian, `21.5°` in English).
- The web app uses the same idea in JavaScript (`t(sr, en)`); the language comes from the panel.
- The default for a fresh panel is `UI_DEFAULT_LANGUAGE` in `include/config.h`.
- Log messages and code comments are not translated.

## Storage

| Where | What |
|---|---|
| NVS namespace `settings` | Wi-Fi, Tuya/eWeLink keys, layout positions, pages, disabled devices, screen timeouts, alarm PIN (salt + SHA-256), interface language (`lang`) |
| RTC memory (survives a software restart only) | The current web PIN, so the phone stays logged in when the panel restarts to change language |
| NVS namespace `ewelink` | eWeLink access/refresh token, region, expiry |
| LittleFS (`spiffs` partition, ~10.9 MB) | `/bg.rgb565`, `/bg1.rgb565` … (768,000 B each), `/scenes.json` |
| Firmware | `assets/background.rgb565` (built-in first-page photo), Montserrat TTF |

The initial layout for a fresh panel comes from `HOME_POSITIONS` (an empty example in `include/config.h`;
put your own device keys in the git-ignored `include/home_positions.h`, which `config.h` includes if it exists), which is also
used by **Default layout** in the web app. Optional name overrides are in `NAME_OVERRIDES`.

## Diagnostics

- **Boot log**: prints the reset reason (`[main] razlog pokretanja: …`). After a crash it also prints
  a summary from the coredump partition: task, PC and backtrace, and whether the dump comes from the
  same firmware build.
  - Decode the addresses:
    ```bash
    xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/panel/firmware.elf <addresses>
    ```
  - Full dump:
    ```bash
    esptool read-flash 0xFF0000 0x10000 core.bin
    esp-coredump info_corefile -t raw -c core.bin .pio/build/panel/firmware.elf
    ```
- **Self-test**: at every boot the signing code is checked against test vectors; look for
  `[selftest] SVE OK`.
- **Cloud log**:
  - `[cloud] Tuya: N prekidaca (ms), heap …` on every refresh (log level info).
  - Errors: `[cloud] <cloud> greska: …`.
  - eWeLink linking steps: `[web] eWeLink: …`. OAuth codes and tokens are never printed.
- **First-sight DP dumps**: `[termostat]`, `[vrata]` (door), `[pokret]` (motion) and `[dim]` (smoke)
  lines show all DPs of a newly seen device. Unsupported devices print `[tuya] nije podrzan: …`.

## Testing tips

- **UI without real devices**: `show` (or `d`, `dc`, `ds`, `dt`), then screenshots with
  `tools/screenshot.py`. Polling is paused in demo mode, and commands to `demo-*` keys are handled
  locally.
- **Parser changes**: add a sample JSON to `parseSamples()` in `src/debug/console.cpp` and run `tp`.
- **Web app without the panel**:
  1. Extract the string from `web_app_html.h` into an `index.html`.
  2. Serve it with a tiny mock server (a few lines of Python `http.server`) that returns a saved
     `/api/state` JSON and accepts the POSTs.
  3. Load it in a desktop browser or in headless Chrome
     (`--headless=new --screenshot --window-size=390,844`) for phone-sized screenshots. Headless
     Chrome with `--enable-logging=stderr` also shows JavaScript errors.
- **After changing `web_app_html.h`**, load the app once before flashing. A JS syntax error, for
  example a stray `//` comment that swallows the rest of a line, breaks the whole page.
