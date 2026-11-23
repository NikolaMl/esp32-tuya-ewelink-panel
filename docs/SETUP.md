# Setup guide

This guide goes from a new board to a working panel with your Tuya and/or eWeLink devices.

1. [Toolchain](#1-toolchain)
2. [Build and flash](#2-build-and-flash)
3. [Optional: compile-time defaults (`secrets.h`)](#3-optional-compile-time-defaults-secretsh)
4. [Wi-Fi](#4-wi-fi)
5. [Tuya / Smart Life](#tuya--smart-life)
6. [eWeLink / Sonoff](#ewelink--sonoff)
7. [Phone web app](#7-phone-web-app)
8. [Home screen photos](#8-home-screen-photos)
9. [Restoring the factory firmware](#9-restoring-the-factory-firmware)

---

## 1. Toolchain

- [PlatformIO](https://platformio.org/install), either the VS Code extension or the `pio` CLI.
- The project uses the community **pioarduino** platform (Arduino-ESP32 3.x on ESP-IDF 5.x), pinned in
  [platformio.ini](../platformio.ini):
  ```ini
  platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
  ```
  PlatformIO downloads it, together with **LVGL 9.2.2** and **ArduinoJson 7.2.1**, on the first build.
- A USB cable. The board has a CH340 USB-serial chip, so on some systems you may need to install the
  CH340 driver.

## 2. Build and flash

```bash
cd esp32-tuya-ewelink-panel
pio run                  # build only
pio run -t upload        # build + flash
pio device monitor       # serial log (115200 baud, with exception decoder)
```

- Upload speed is set to **230400** baud in `platformio.ini`. With the CH340 on macOS, higher speeds
  (460800/921600) were unreliable. Raise it if your setup allows.
- The first build takes a few minutes, because LVGL and the Montserrat font are compiled.
- Flash layout ([partitions.csv](../partitions.csv)):

  | Partition | Size | Used for |
  |---|---|---|
  | `nvs` | 20 KB | settings, cloud keys, eWeLink tokens, layout |
  | `app0` | 5 MB | firmware (single app slot, no OTA) |
  | `spiffs` (LittleFS) | ~10.9 MB | photos uploaded from the phone, `/scenes.json` |
  | `coredump` | 64 KB | crash dump, summarized in the log on the next boot |

> Changing `partitions.csv` erases the LittleFS contents (uploaded photos and scenes). NVS settings
> keep their offset and survive.

## 3. Optional: compile-time defaults (`secrets.h`)

You can enter everything on the panel or in the web app, so this step is optional. If you flash
many times during development, it can save typing:

```bash
cp include/secrets.example.h include/secrets.h
```

Fill in `WIFI_SSID`, `WIFI_PASSWORD`, `TUYA_ACCESS_ID`, `TUYA_ACCESS_SECRET`, `EWELINK_APP_ID` and
`EWELINK_APP_SECRET` as needed.

- These are only **initial** values. Anything saved on the panel (in NVS) takes precedence.
- `include/secrets.h` is in `.gitignore`. Never commit it.

## 4. Wi-Fi

On the panel: **gear icon → Wi-Fi → Skeniraj** (scan). Pick your network, type the password on the
on-screen keyboard and tap **Sačuvaj i poveži** (save & connect).

- The ESP32-S3 supports **2.4 GHz only**.
- After it connects, the Wi-Fi tab shows the IP address.
- Recommended: create a **DHCP reservation** (static lease) for the panel in your router. A fixed IP
  makes the web app address and the eWeLink redirect URL stable (see below).

---

## Tuya / Smart Life

The panel uses the **Tuya Cloud OpenAPI** with your own free cloud project. Your Smart Life
(or Tuya Smart) app account is linked to that project, which gives the project access to your devices.

1. Create an account on [iot.tuya.com](https://iot.tuya.com). It can use the same email as your
   Smart Life account.
2. Go to **Cloud → Development → Create Cloud Project**:
   - **Development Method**: Smart Home.
   - **Data Center**: the same region as your Smart Life account. For most of Europe this is
     **Central Europe**. The panel supports:

     | Data center | Endpoint |
     |---|---|
     | Central Europe | `https://openapi.tuyaeu.com` |
     | Western Europe | `https://openapi-weaz.tuyaeu.com` |
     | Western America | `https://openapi.tuyaus.com` |
     | Eastern America | `https://openapi-ueaz.tuyaus.com` |
     | China | `https://openapi.tuyacn.com` |
     | India | `https://openapi.tuyain.com` |
3. In the project, open **Devices → Link App Account → Add App Account** and scan the QR code with the
   Smart Life app (**Me** → scan icon, top right). Your devices now appear in the project.
4. Under **Service API**, make sure **IoT Core** (and *Authorization Token Management*) are
   subscribed.
5. On the project's **Overview**, copy the **Access ID / Client ID** and the
   **Access Secret / Client Secret**.
6. Enter them on the panel (**Podešavanja → Tuya**) or, easier, in the web app (**Nalozi** tab). Pick
   the same data center there.

Within a few seconds the **Tuya** dot in the device list turns green and your devices appear.

**New devices**: a device you add to the Smart Life app later shows up on the panel at the next
refresh. No extra step is needed, because the panel always reads the full device list of the linked
account.

**Trial plan**: the free IoT Core *Trial* lasts a few months and has a monthly API call quota.
- When it expires, the panel shows `Tuya 28841002: Tuya trial je istekao`.
- Extend it on iot.tuya.com: **Cloud → Cloud Services → IoT Core → Extend Trial Period**. Approval
  usually takes a few hours.
- To save calls, the panel refreshes every 10 s only while someone uses it, and every 5 min
  otherwise.

**Common errors**

| Code | Meaning |
|---|---|
| 1004 | Wrong Access Secret (sign invalid). |
| 1013 | Panel clock is wrong. It waits for NTP; check internet access. |
| 1106 | No permission: the app account is not linked to the project. |
| 2008 | The command is not supported by this device. |
| 28841101 | IoT Core service is not subscribed. |
| 28841105 | Wrong data center, or IoT Core is missing. |

---

## eWeLink / Sonoff

The panel uses the **eWeLink Cloud API v2** with a free **personal developer** app. A personal app
can only sign in with **OAuth2 in a browser**, so linking works like this: the panel shows a QR code,
you sign in to eWeLink on your phone, and eWeLink redirects the browser back to the panel. The panel
then receives its tokens.

### 1. Become a developer

Register on [dev.ewelink.cc](https://dev.ewelink.cc) as a **personal developer**. Approval arrives by
email, usually within 1–3 working days.

### 2. Find the panel's Redirect URL

On the panel open **Podešavanja → eWeLink**, or in the web app the **Nalozi** tab. Both show the
**Redirect URL**, for example:

```
http://192.168.1.50/ewelink/callback
```

### 3. Create an app

On dev.ewelink.cc, create an app and enter **exactly** that URL as its **Redirect URL**: plain `http`,
the panel's IP address, and no trailing slash. A new app can take 5–15 minutes to become active.

### 4. Enter the keys

Copy the app's **App ID** and **App Secret** to the panel (**Podešavanja → eWeLink → Sačuvaj**) or to
the web app (**Nalozi** tab → eWeLink → **Sačuvaj**). Copy-paste in the web app on a computer is the
easiest way.

### 5. Link your account

Tap **Poveži nalog** (link account) in either place:

- **On the panel**: the panel shows a QR code. Scan it with a phone on the **same Wi-Fi** (not on
  mobile data). The link is valid for 10 minutes.
- **In the web app**: the button opens the eWeLink sign-in page in the same browser.

Sign in with the account you use in the eWeLink app. eWeLink sends the browser back to the panel,
which shows **"Povezano!"** (connected). Within seconds the **eWeLink** dot turns green and your
Sonoff devices appear. In the web app they carry a blue **e** icon.

### Tokens and lifetime

- The access token is valid for 30 days. The panel renews it automatically about a week before it
  expires.
- The refresh token is valid for 60 days and is replaced on every renewal. As long as the panel
  runs, it stays signed in indefinitely.
- If the panel is off for **more than 60 days**, the session expires
  ("eWeLink: prijava istekla - ponovo poveži nalog"). Tap **Poveži nalog** again.
- **Odjavi** (sign out) deletes the stored tokens.

### Does the IP address matter later?

Only while linking. After linking, the panel talks to eWeLink with its stored tokens and never uses the
Redirect URL again. A different IP later does not break anything. You would only need to update the
Redirect URL on dev.ewelink.cc if you ever link the account again from a new IP. A DHCP reservation
avoids that.

### Limits

- The free personal app allows **50,000 API calls per month** and is valid for **one year**. After
  that, renew or recreate it on dev.ewelink.cc.
- Only devices returned by the eWeLink API (Sonoff/CoolKit devices) appear. Third-party devices added
  to the eWeLink app don't.

### If linking fails

| What you see | Cause |
|---|---|
| The sign-in page never returns to the panel | The phone is not on the home Wi-Fi, or the Redirect URL on dev.ewelink.cc doesn't match exactly. |
| "Link je istekao" (link expired) | The QR link is older than 10 minutes. Tap **Poveži nalog** again. |
| "Nevažeći ili istekao zahtev" (invalid or expired request) | The sign-in took too long, or an old tab was used. Start again. |
| "Panel još nema tačno vreme" (no correct time yet) | The panel hasn't synced NTP yet. Wait a few seconds. |
| "Prijava nije uspela: …" (sign-in failed) | The error from eWeLink follows, e.g. wrong App Secret. |

The serial log shows each step (`[web] eWeLink: …`), but never the one-time code or the tokens.

---

## 7. Phone web app

1. On the panel open **Podešavanja → Web**, or long press the home screen photo and tap
   **Na telefonu**.
2. Scan the QR code with a phone on the **same Wi-Fi**. The URL contains the PIN, so the app opens
   directly.
3. If you open `http://<panel-ip>/` by hand, enter the 6-digit PIN shown under **Podešavanja → Web**.

How the PIN works:

- The PIN is **new at every boot**. After a reboot or a new flash, scan the QR code again; an old
  tab asks for the new PIN by itself.
- After **10 different wrong PINs**, the web app is locked for **60 s**.
- If an alarm PIN is set, **Podešavanja** on the panel (and therefore the QR code) is protected by
  that PIN.

The tabs are described in the [README](../README.md#phone-web-app).

---

## 8. Home screen photos

Each home screen page (up to 6) can have its own photo.

### From the phone (recommended)

In the web app, open **Raspored**, select the page and tap **📷 Fotografija**:

1. Pick a photo.
2. Crop it to 800×480 with your finger or the sliders. The existing markers are drawn on top, so you
   can line them up.
3. Send it to the panel.

Photos are stored in LittleFS (`/bg.rgb565`, `/bg1.rgb565`, …, 768,000 bytes each). **Vrati ugrađenu**
switches the first page back to the built-in image.

> Writing a photo to flash can make the screen glitch for a moment. That's expected; see
> [DEVELOPMENT.md](DEVELOPMENT.md#display-pipeline-and-flicker).

### Built-in default image

The first page falls back to an image compiled into the firmware: `assets/background.rgb565`
(800×480, RGB565 little-endian, embedded via `board_build.embed_files`). To replace it:

```bash
python3 tools/make_background.py ~/Pictures/house.jpg   # scale + center-crop to 800x480, dithered RGB565
python3 tools/make_background.py --placeholder          # simple gradient
pio run -t upload
```

`make_background.py` uses only the Python standard library and macOS `sips`.

The repository ships a drawn demo house (`assets/demo-house.svg`, rendered to `assets/demo-house.png`).
The easiest way to use your own house is to upload its photo from the phone (**Raspored → 📷 Fotografija**):
it is stored on the panel, survives firmware updates and never ends up in the repository. If you do
bake your own photo into the firmware, keep the source photo out of git (for example in the ignored
`backup/` folder) before you publish a fork.

---

## 9. Restoring the factory firmware

Before the first flash, you may want to back up the board's original firmware:

```bash
~/.platformio/penv/bin/esptool --port /dev/cu.usbserial-XXXX --baud 230400 \
    read-flash 0x0 0x1000000 backup/factory_full.bin
```

To restore it later:

```bash
~/.platformio/penv/bin/esptool --port /dev/cu.usbserial-XXXX --baud 230400 \
    write-flash 0x0 backup/factory_full.bin
```

`backup/` is git-ignored.
