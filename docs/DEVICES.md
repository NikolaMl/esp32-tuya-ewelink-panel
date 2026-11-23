# Supported devices

The panel reads the **standard data points (DPs)** that the Tuya cloud reports for each device (the
"Standard Instruction Set" on developer.tuya.com) and the `params` of eWeLink devices. It does not use
local protocols.

- [How a Tuya device is recognized](#how-a-tuya-device-is-recognized)
- [Switches and lights](#switches-and-lights)
- [Blinds / covers](#blinds--covers)
- [Thermostats](#thermostats)
- [Temperature sensors](#temperature-sensors)
- [Door / window and motion / presence sensors](#door--window-and-motion--presence-sensors)
- [Smoke detectors](#smoke-detectors)
- [Alarm hub](#alarm-hub)
- [eWeLink devices](#ewelink-devices)
- [Groups in the device list](#groups-in-the-device-list)
- [Not supported](#not-supported)
- [Adding support for a new device](#adding-support-for-a-new-device)

---

## How a Tuya device is recognized

Each device in the account's device list (`GET /v1.0/iot-01/associated-users/devices`) goes through
these checks in order. The first match wins
([`TuyaClient::parseDevice`](../src/net/tuya_client.cpp)):

| # | Check | Becomes |
|---|---|---|
| 1 | category `mal` | **alarm hub** |
| 2 | category `cl` / `clkg`, or any DP `control[_N]` with value `open`/`stop`/`close`/`continue` | **blind / cover** |
| 3 | category `ywbj` | **smoke detector** |
| 4 | category `wk` / `wkf`, or a DP `temp_set` on a device whose category is *not* in the switch list | **thermostat** |
| 5 | DP `doorcontact_state`, `pir` or `presence_state` on a device whose category is *not* in the switch list | **door / motion sensor** |
| 6 | a current-temperature DP | **temperature sensor** (in addition to step 7) |
| 7 | category in `TUYA_CATEGORIES` and boolean `switch*` DPs | **switch / light** (one tile per channel) |
| – | anything else | logged once: `[tuya] nije podrzan: <name> (kategorija X) DP: …` |

Steps 4 and 5 check the switch list on purpose. A device from a switch category stays a switch, for
example a socket with a thermostat (`wkcz`) or a lamp with a motion sensor (`dj` + `pir`).

---

## Switches and lights

**Categories** (`TUYA_CATEGORIES` in [include/config.h](../include/config.h)):

| Group | Categories |
|---|---|
| Lights | `dj` bulb, `dd` LED strip, `xdd` ceiling light, `fwd` ambient light, `dc` string lights, `tgq` dimmer, `tgkg` dimmer switch, `gyd` light with motion sensor, `sxd` spotlight, `fsd` ceiling fan with light |
| Switches | `kg` wall switch, `tdq` switch module, `dlq` smart breaker, `cjkg` scene switch, `ckqdkg` card switch |
| Sockets | `cz` socket, `pc` power strip, `wkcz` socket with thermostat |
| Other | `szjqr` fingerbot, `sfkzq` water valve |

**DPs**: `switch`, `switch_led`, `switch_N`, `switch_led_N`, where N is 1–2 digits and the value is a
boolean.
- Codes like `switch_backlight`, `switch_usb1` or `switch_inching` are ignored.
- A device with several channels gives one tile per channel, named `<device> <N>`.
- The key of a channel is `<deviceId>/<DP code>`; a single-channel device uses just `<deviceId>`.

**Commands**: `POST /v1.0/devices/{id}/commands` with `{code: <DP>, value: true|false}`.

Thermostats and air conditioners are deliberately **not** in the switch list. Otherwise **Ugasi
svetla** (all lights off) or a stray tap could switch off the heating. They get their own thermostat
handling instead.

To support more switch categories, add them to `TUYA_CATEGORIES`. Set `"*"` to accept every category
that has `switch*` DPs.

---

## Blinds / covers

| | |
|---|---|
| Categories | `cl` (curtain), `clkg` (curtain switch), or any device with a `control` DP whose value is `open`/`stop`/`close`/`continue` |
| Control DP | `control`, or `control_2` etc. on double modules (one tile per channel) |
| Position DP | `percent_state` (preferred) or `percent_control`, 0–100. If the module isn't calibrated and reports no position, only the arrows are shown. |
| Commands | `open`, `stop`, `close` |

---

## Thermostats

| | |
|---|---|
| Categories | `wk` thermostat, `wkf` radiator valve (TRV), or any device with `temp_set` that isn't in `TUYA_CATEGORIES` (heaters, AC units, …) |
| Power | `switch` (boolean). If it's missing, the pop-up has no on/off button. |
| Target temperature | `temp_set` |
| Current temperature | first available of `va_temperature`, `temp_current`, `temp_indoor`, `cur_temp`, `temperature` |
| Mode (display only) | `mode`, shown in Serbian: `auto`/`program`/`schedule`/`smart` = *po rasporedu* (schedule), `manual`/`hold` = *ručno* (manual), `eco`, `holiday`/`away`, `comfort`, `frost`/`antifreeze`, `hot`/`heat`, `cold`/`cool`, `wind`/`fan`, `dry` |
| Heating indicator | `work_state` containing `heat`/`warm` (or `hot`), `valve_state` = `open`, `valve_open_degree` / `valve_opening` > 0 |

**Scale and range**:
- The panel reads `/v1.0/devices/{id}/specifications` **once per device** and caches it. That gives
  the `scale` (decimals), `min`, `max` and `step` of `temp_set`, and the scale of the current
  temperature.
- Without a specification it guesses: a `temp_set` above 45 means tenths of a degree, and a current
  value above 60 means tenths.
- A tap on **−**/**+** changes the target by 0.5° or by the device's step, whichever is larger.
- When sending, the value is scaled, rounded to the device's step and clamped to its range.

**Commands**: `switch` and/or `temp_set` in a single request. The pop-up sends 1.2 s after your last
tap.

**Schedule mode**: a thermostat running its own schedule may put its target back later. The pop-up
shows a hint when the mode is a schedule.

---

## Temperature sensors

Any device with a current-temperature DP becomes a temperature tile, display only. That includes
`wsdcg` temperature/humidity sensors and switches with a built-in sensor.

- DP priority: `va_temperature`, `temp_current`, `temp_indoor`, `cur_temp`, `temperature`.
- `temp_value` (a bulb's color temperature) and `temp_set` are never treated as a measurement.
- **Scale**: read once from the device specification. Without one, `va_temperature` or a raw value of
  100 or more means tenths of a degree.
- `temp_unit_convert` = `f` marks a Fahrenheit device.

---

## Door / window and motion / presence sensors

| Kind | DP | Active when |
|---|---|---|
| Door / window (`mcs`) | `doorcontact_state` | `true` (also accepts `"open"` / `"1"`) → *Otvoreno* (open) |
| Motion (`pir`) | `pir` | `"pir"` → *Pokret* (motion) |
| Presence, mmWave (`hps`) | `presence_state` | `"presence"` → *Pokret* |

Extra DPs that are shown:
- `battery_percentage` / `va_battery`, with "low" at 20% or less;
- `battery_state` = `low`;
- `temper_alarm` (removed from its mount).

The tile shows when the state changed: *Otvoreno od 14:32* (open since 14:32), or for motion
*poslednji u 14:32* (last at 14:32).

> State is read on the normal refresh (10 s while in use, 5 min while idle). A short motion between two
> refreshes can be missed. These tiles are for information, not for intrusion detection.

---

## Smoke detectors

| | |
|---|---|
| Category | `ywbj` |
| Smoke | `smoke_sensor_status` = `alarm`, or `smoke_sensor_state` = `"1"` |
| Battery | `battery_percentage` or `battery_state` (`low`) |
| Tamper | `temper_alarm` |
| Self-test | `checking_result` = `check_failure` → *Samotest nije prošao* (self-test failed) |

The tile shows *U redu* (OK), *DIM!* (smoke), *Slaba baterija* (low battery), *Skinut sa nosača*
(removed from mount), *Samotest nije prošao* or *Nedostupan* (offline).

Smoke opens the red full-screen warning. The `dl` debug command prints the last 7 days of smoke DP
history from the Tuya cloud.

---

## Alarm hub

| | |
|---|---|
| Category | `mal` |
| Mode | `master_mode`: `disarmed`, `arm` (away), `home`, `sos` |
| Alarm | `master_state` = `alarm` |
| Exit delay | `delay_set` (seconds). The panel counts it down after arming. |
| Entry delay | `alarm_delay_time` (seconds) |
| Last event | `alarm_msg`, decoded and shown on the alarm screen, e.g. the zone name |
| Commands | `master_mode` = `arm` / `home` / `disarmed` |

The panel does not read the hub's sensitive DPs: keypad password, alarm call and SMS phone numbers,
and sub-admin data. It never logs or displays them.

---

## eWeLink devices

All devices from `GET /v2/device/thing` (item types 1 and 2; groups are skipped) that have:

- `params.switch` (`"on"`/`"off"`): a single-channel device, or
- `params.switches[]` (`{outlet, switch}`): a multi-channel device. The number of channels comes
  from, in order:
  1. the UIID table below;
  2. the channel names set in the app (`tags.ck_channel_name`);
  3. the array length (max 4).

| UIID | Channels |
|---|---|
| 2, 7, 29, 82, 113, 126, 139, 161, 165, 289, 2256 | 2 |
| 3, 8, 83, 114, 140, 162, 3256 | 3 |
| 4, 9, 31, 84, 130, 141, 163, 290, 4256 | 4 |
| 77, 78, 81, 107, 112, 138, 160, 182 | 1 (uses `switches[]`) |

- Channel names: `<device> - <channel name from the app>`, or `<device> <N>`.
- Keys: `<deviceid>` or `<deviceid>/<outlet>`, with outlets counted from 0.
- Commands: `POST /v2/device/thing/status` with `switch` or `switches:[{outlet, switch}]`.
- API host by region: `https://<region>-apia.coolkit.cc` (`eu`, `us`, `as`) or
  `https://cn-apia.coolkit.cn`. The region comes from the OAuth sign-in.

Only switching is supported for eWeLink. Sensors such as the Sonoff SNZB series, and thermostats, are
not read yet.

---

## Groups in the device list

The list section comes from the Tuya category or the eWeLink UIID
([src/model/device_group.cpp](../src/model/device_group.cpp)). Blinds, thermostats, temperature sensors,
smoke, door/motion sensors, the alarm hub and scenes have their own sections.

| Group | Tuya categories | eWeLink UIIDs |
|---|---|---|
| **Svetla** (Lights) | `dj dd xdd fwd dc tgq tgkg gyd sxd tyndj fsd tyd` | 22, 36, 44, 57, 59, 103, 104, 135, 136, 137, 173 (bulbs, D1 dimmer, L1/L2 strips) |
| **Prekidači** (Switches) | `kg tdq dlq cjkg ckqdkg wxkg` | all others (Basic, Mini, T-series, Dual, …) |
| **Utičnice** (Sockets) | `cz pc wkcz` | 5, 32, 182, 190, 262, 276, 277 (POW, S26/S40, POWR3, …) |
| **Klima** (Climate) | `wk wkf wsdcg kt ktkzq qn fs fskg jsq cs kj xfj rs bh` | 15, 181 (TH / THR3) |
| **Sigurnost** (Security) | `mal ywbj rqbj cobj sj mcs pir hps sgbj sp ms jtmspro sos` | – |
| **Ostalo** (Other) | everything else | – |

**Ugasi svetla** (all lights off) switches off only the **Lights** and **Switches** groups.

---

## Not supported

At the moment the panel does not support:

- Gas and CO detectors, water leak sensors, sirens, locks, cameras and SOS buttons.
  - Their categories are listed under Security for grouping, but only `ywbj` smoke detectors have
    dedicated handling.
  - If such a device has `switch*` DPs and its category is added to `TUYA_CATEGORIES`, it shows as
    a switch.
- IR blasters, gateways, pet feeders and other devices without standard switch DPs.
- Dimming, colour and fan speed: lights are on/off only.
- eWeLink sensors and thermostats.

Every unsupported Tuya device is logged **once** at startup:

```
[tuya] nije podrzan: <device name> (kategorija <category>) DP: <code> <code> ...
```

---

## Adding support for a new device

1. **Look at the log.** Unsupported devices print their category and DP codes. Thermostats and
   door/motion sensors print **all DPs with values** the first time they are seen, e.g.
   `[termostat] <name> (kategorija wk, online): switch=true temp_set=220 temp_current=215 …`.
2. **Try it offline.** The `tp` debug command runs the parser on built-in sample JSON responses
   (`wk`, `wkf`, `kt`, `wkcz`, `mcs`, `pir`, `hps`, `dj`) without network access. Add your device's
   JSON to `parseSamples()` in [src/debug/console.cpp](../src/debug/console.cpp) to test the
   recognition.
3. **Wire it up**:
   - a switch-like device usually only needs its category added to `TUYA_CATEGORIES` (and to the
     group table in `device_group.cpp`);
   - a new kind of device needs a model in `src/model/`, a `parse…()` in `tuya_client.cpp`, a
     `replace…()` in `DeviceStore`, a tile in `ui.cpp`/`home_screen.cpp` and an item type in the web
     app (`web_portal.cpp` `handleState` + `web_app_html.h` `CATS`).
4. **Test without hardware.** The debug console can create demo devices that live only in RAM
   (`d`, `dc`, `ds`, `dt`, `show`), so you can check the UI without touching real devices. See
   [DEVELOPMENT.md](DEVELOPMENT.md#serial-debug-console).
