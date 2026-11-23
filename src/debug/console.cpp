#include "debug/console.h"

#include <Arduino.h>
#include <lvgl.h>

#include <esp_heap_caps.h>

#include <algorithm>
#include <vector>

#include "board/display.h"
#include "config.h"
#include "model/device_store.h"
#include "model/scenes.h"
#include "model/settings.h"
#include "net/web_portal.h"
#include "net/cloud_task.h"
#include "net/https_session.h"
#include "net/tuya_client.h"
#include "ui/thermo_popup.h"
#include "ui/alarm_screen.h"
#include "ui/cover_popup.h"
#include "ui/home_screen.h"
#include "ui/settings_screen.h"
#include "ui/ui.h"
#include "util/crypto.h"

void consoleWake();  // main.cpp: komanda iz konzole = dodir (ekran ostaje upaljen)

namespace debug {

static String s_line;
// Ubačen dodir: od (x0,y0) do (x1,y1) tokom dur ms (tap = ista tačka)
static int16_t s_x0 = 0, s_y0 = 0, s_x1 = 0, s_y1 = 0;
static uint32_t s_tStart = 0, s_tDur = 0;

bool injectedTouch(int16_t& x, int16_t& y) {
  const uint32_t el = millis() - s_tStart;
  if (!s_tDur || el >= s_tDur) return false;
  x = s_x0 + (int32_t)(s_x1 - s_x0) * (int32_t)el / (int32_t)s_tDur;
  y = s_y0 + (int32_t)(s_y1 - s_y0) * (int32_t)el / (int32_t)s_tDur;
  return true;
}

static void inject(int x0, int y0, int x1, int y1, uint32_t dur) {
  s_x0 = x0; s_y0 = y0; s_x1 = x1; s_y1 = y1;
  s_tStart = millis();
  s_tDur = dur;
}

// Snimak ekrana: RLE (broj u16, boja RGB565 u16, little-endian), base64 u numerisanim linijama
// "@S <i> ...". Logovi drugih taskova mogu da upadnu u liniju, pa alat posle traži oštećene
// linije ponovo komandom "r <i>" (odgovor "@R <i> ..."). Bafer je u PSRAM-u i čuva se do sledećeg snimka.
static constexpr size_t CHUNK = 90;
static uint8_t* s_rle = nullptr;
static size_t s_rleLen = 0, s_rleCap = 0;

static bool rlePush(uint16_t run, uint16_t color) {
  if (s_rleLen + 4 > s_rleCap) {
    const size_t cap = s_rleCap ? s_rleCap * 2 : 64 * 1024;
    uint8_t* p = static_cast<uint8_t*>(heap_caps_realloc(s_rle, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) return false;
    s_rle = p;
    s_rleCap = cap;
  }
  s_rle[s_rleLen++] = run & 0xFF;
  s_rle[s_rleLen++] = run >> 8;
  s_rle[s_rleLen++] = color & 0xFF;
  s_rle[s_rleLen++] = color >> 8;
  return true;
}

static void sendChunk(const char* tag, size_t idx) {
  const size_t off = idx * CHUNK;
  const size_t n = std::min(CHUNK, s_rleLen - off);
  Serial.printf("%s %u %s\n", tag, (unsigned)idx, crypto::base64(s_rle + off, n).c_str());
}

// scale 2 = pola rezolucije (prosek 2x2): fotografija se slabo pakuje u RLE, a serijska veza je spora.
// Ako RLE ispadne veći od sirovih piksela, šalju se sirovi RGB565 (oznaka "raw" u zaglavlju).
// Gornji sloj (prozorčići, poruke) nije deo ekrana. Za trenutak snimanja se vidljivi elementi
// gornjeg sloja prebace na ekran (iznad svega), pa vrate — bez posebnog snimka gornjeg sloja
// (ARGB8888 800x480 = 1,5 MB u jednom komadu, koliko PSRAM posle rada često nema).
static lv_draw_buf_t* snapshotWithTopLayer() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_t* top = lv_layer_top();
  std::vector<lv_obj_t*> moved;
  for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
    lv_obj_t* c = lv_obj_get_child(top, i);
    if (!lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) moved.push_back(c);
  }
  for (lv_obj_t* c : moved) lv_obj_set_parent(c, scr);  // redom -> isti raspored po dubini
  lv_draw_buf_t* buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
  for (lv_obj_t* c : moved) lv_obj_set_parent(c, top);
  return buf;
}

static void screenshot(int scale) {
  lv_draw_buf_t* buf = snapshotWithTopLayer();
  if (!buf) {
    Serial.println("@ERR snapshot");
    return;
  }
  const uint32_t sw = buf->header.w, sh = buf->header.h, stride = buf->header.stride;
  const uint32_t w = sw / scale, h = sh / scale;
  auto pixel = [&](uint32_t x, uint32_t y) -> uint16_t {
    if (scale == 1) return reinterpret_cast<const uint16_t*>(buf->data + y * stride)[x];
    uint32_t r = 0, g = 0, b = 0;
    for (int dy = 0; dy < 2; dy++) {
      const uint16_t* row = reinterpret_cast<const uint16_t*>(buf->data + (y * 2 + dy) * stride);
      for (int dx = 0; dx < 2; dx++) {
        const uint16_t c = row[x * 2 + dx];
        r += c >> 11;
        g += (c >> 5) & 0x3F;
        b += c & 0x1F;
      }
    }
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
  };

  s_rleLen = 0;
  bool ok = true, raw = false;
  uint16_t cur = 0, run = 0;
  for (uint32_t y = 0; y < h && ok; y++) {
    for (uint32_t x = 0; x < w; x++) {
      const uint16_t px = pixel(x, y);
      if (run && px == cur && run < 0xFFFF) {
        run++;
      } else {
        if (run && !(ok = rlePush(run, cur))) break;
        cur = px;
        run = 1;
      }
    }
  }
  if (ok && run) ok = rlePush(run, cur);
  if (ok && s_rleLen > w * h * 2) {  // sirovi pikseli su manji
    raw = true;
    s_rleLen = 0;
    for (uint32_t y = 0; y < h && ok; y += 1) {
      for (uint32_t x = 0; x + 1 < w; x += 2) {
        if (!(ok = rlePush(pixel(x, y), pixel(x + 1, y)))) break;  // 2 piksela = 4 bajta
      }
    }
  }
  lv_draw_buf_destroy(buf);
  if (!ok) {
    Serial.println("@ERR memorija");
    return;
  }

  const size_t chunks = (s_rleLen + CHUNK - 1) / CHUNK;
  Serial.printf("\n@H %u %u %u %u%s\n", (unsigned)w, (unsigned)h, (unsigned)s_rleLen, (unsigned)chunks,
                raw ? " raw" : "");
  for (size_t i = 0; i < chunks; i++) sendChunk("@S", i);
  Serial.println("@E");
}

static void demo() {
  struct D { CloudKind c; const char* id; const char* ch; bool multi; const char* name; bool on; bool online; };
  static const D items[] = {
      {CloudKind::Tuya, "demo-t1", "switch_1", false, "Dnevna soba", true, true},
      {CloudKind::Tuya, "demo-t2", "switch_1", true, "Hodnik 1", false, true},
      {CloudKind::Tuya, "demo-t2", "switch_2", true, "Hodnik 2", true, true},
      {CloudKind::Tuya, "demo-t3", "switch_led", false, "Spavaća soba", false, true},
      {CloudKind::Tuya, "demo-t4", "switch_1", false, "Трпезарија", false, true},
      {CloudKind::EWeLink, "demo-e1", "", false, "Kuhinja", true, true},
      {CloudKind::EWeLink, "demo-e2", "", false, "Terasa", false, false},
      {CloudKind::EWeLink, "demo-e3", "0", true, "Dečija soba - plafon", false, true},
      {CloudKind::EWeLink, "demo-e3", "1", true, "Dečija soba - lampa", false, true},
      {CloudKind::EWeLink, "demo-e4", "", false, "Kupatilo ogledalo", false, true},
  };
  std::vector<SwitchInfo> tuya, ew;
  for (const D& d : items) {
    SwitchInfo s;
    s.cloud = d.c;
    s.deviceId = d.id;
    s.channel = d.ch;
    s.isMultiChannel = d.multi;
    s.name = d.name;
    s.on = d.on;
    s.online = d.online;
    (d.c == CloudKind::Tuya ? tuya : ew).push_back(s);
  }
  cloud::setDemoMode(true);
  g_store.replaceCloud(CloudKind::Tuya, tuya);
  g_store.replaceCloud(CloudKind::EWeLink, ew);
  g_store.setCloudStatus(CloudKind::Tuya, LinkState::Ok);
  g_store.setCloudStatus(CloudKind::EWeLink, LinkState::Ok);
  Serial.println("@OK demo");
}

// Probna roletna (za proveru ekrana bez pravog modula); ostaje do restarta
static void demoCover() {
  CoverInfo c;
  c.deviceId = "demo-c1";
  c.name = "Roletna dnevna";
  c.online = true;
  c.position = 40;
  c.lastAction = "stop";
  cloud::setDemoMode(true);
  g_store.replaceCovers({c});
  Serial.println("@OK demo roletna");
}

// Probni detektor dima (do restarta): "ds" = javlja dim, "ds0" = u redu
static void demoSmoke(bool alarm) {
  SmokeInfo s;
  s.deviceId = "demo-d1";
  s.name = "Detektor dnevna soba";
  s.online = true;
  s.alarm = alarm;
  s.battery = 85;
  cloud::setDemoMode(true);
  g_store.replaceSmokes({s});
  Serial.printf("@OK demo dim %s\n", alarm ? "ALARM" : "u redu");
}

// Probni termostat, senzor vrata i senzor pokreta (do restarta) i njihove oznake na prikazanom
// delu slike (samo u RAM-u; sačuvan raspored se ne menja)
static void demoNew() {
  ThermostatInfo t;
  t.deviceId = "demo-t1";
  t.name = "Termostat dnevna";
  t.online = true;
  t.hasPower = true;
  t.on = true;
  t.hasCurrent = true;
  t.current = 20.5f;
  t.hasTarget = true;
  t.target = 22;
  t.minT = 5;
  t.maxT = 30;
  t.step = 0.5f;
  t.heating = 1;
  t.mode = "manual";
  BinarySensorInfo d;
  d.deviceId = "demo-v1";
  d.name = "Ulazna vrata";
  d.kind = BinaryKind::Door;
  d.online = true;
  d.active = true;
  d.battery = 90;
  BinarySensorInfo m;
  m.deviceId = "demo-m1";
  m.name = "Hodnik pokret";
  m.kind = BinaryKind::Motion;
  m.online = true;
  m.battery = 15;
  m.batteryLow = true;
  cloud::setDemoMode(true);
  g_store.replaceThermostats({t});
  g_store.replaceBinaries({d, m});
  home_ui::debugPlace("demo-t1", 150, 300);
  home_ui::debugPlace("demo-v1", 400, 300);
  home_ui::debugPlace("demo-m1", 640, 300);
  Serial.println("@OK demo termostat/vrata/pokret");
}

// Probna vrata / pokret: promeni stanje (kao da je senzor javio)
static void demoToggle(const char* key) {
  std::vector<BinarySensorInfo> list;
  for (const BinarySensorState& b : g_store.binaries()) {
    BinarySensorInfo x = b;
    if (x.deviceId == key) x.active = !x.active;
    list.push_back(x);
  }
  g_store.replaceBinaries(list);
  Serial.printf("@OK %s\n", key);
}

// Demo prikaz za snimke ekrana (README): izmišljeni uređaji, scene i oznake na ugrađenoj slici kuće.
// Samo u RAM-u: čitanje sa Tuya/eWeLink naloga staje, sačuvan raspored, scene i fotografije se ne menjaju.
// Sve je kao pre posle restarta. "show d" = detektor dima javlja dim (crveno upozorenje).
static void showcase(bool smoke) {
  cloud::setDemoMode(true);  // prvo: poll više ne upisuje prave uređaje
  struct Sw { CloudKind c; const char* id; const char* ch; bool multi; const char* name; bool on; bool online; DeviceGroup g; };
  static const Sw sws[] = {
      {CloudKind::Tuya, "demo-l1", "", false, "Living room", true, true, DeviceGroup::Lights},
      {CloudKind::Tuya, "demo-l2", "", false, "Bedroom", true, true, DeviceGroup::Lights},
      {CloudKind::Tuya, "demo-l3", "", false, "Kids room", false, true, DeviceGroup::Lights},
      {CloudKind::Tuya, "demo-l5", "", false, "Porch", true, true, DeviceGroup::Lights},
      {CloudKind::Tuya, "demo-h1", "switch_1", true, "Hallway 1", false, true, DeviceGroup::Switches},
      {CloudKind::Tuya, "demo-h1", "switch_2", true, "Hallway 2", true, true, DeviceGroup::Switches},
      {CloudKind::Tuya, "demo-p1", "", false, "Coffee machine", false, true, DeviceGroup::Sockets},
      {CloudKind::Tuya, "demo-p2", "", false, "TV", true, true, DeviceGroup::Sockets},
      {CloudKind::EWeLink, "demo-l4", "", false, "Kitchen", true, true, DeviceGroup::Lights},
      {CloudKind::EWeLink, "demo-e1", "", false, "Garden lights", true, true, DeviceGroup::Lights},
      {CloudKind::EWeLink, "demo-e2", "", false, "Pool pump", false, true, DeviceGroup::Sockets},
      {CloudKind::EWeLink, "demo-e3", "", false, "Terrace", false, false, DeviceGroup::Lights},
  };
  std::vector<SwitchInfo> tuya, ew;
  for (const Sw& d : sws) {
    SwitchInfo s;
    s.cloud = d.c;
    s.deviceId = d.id;
    s.channel = d.ch;
    s.isMultiChannel = d.multi;
    s.name = d.name;
    s.on = d.on;
    s.online = d.online;
    s.group = d.g;
    (d.c == CloudKind::Tuya ? tuya : ew).push_back(s);
  }
  g_store.replaceCloud(CloudKind::Tuya, tuya);
  g_store.replaceCloud(CloudKind::EWeLink, ew);

  AlarmInfo a;
  a.deviceId = "demo-a1";
  a.name = "House alarm";
  a.online = true;
  a.mode = AlarmMode::Home;
  a.exitDelay = 30;
  a.entryDelay = 30;
  g_store.replaceAlarms({a});

  std::vector<SensorInfo> sensors;
  const struct { const char* id; const char* name; float v; } temps[] = {
      {"demo-s1", "Outside", 8.4f}, {"demo-s2", "Living room", 21.8f}, {"demo-s3", "Bedroom", 20.6f},
      {"demo-s4", "Basement", 15.2f}};
  for (const auto& t : temps) {
    SensorInfo x;
    x.deviceId = t.id;
    x.name = t.name;
    x.online = true;
    x.hasValue = true;
    x.value = t.v;
    sensors.push_back(x);
  }
  g_store.replaceSensors(sensors);

  CoverInfo c1, c2;
  c1.deviceId = "demo-c1";
  c1.name = "Garage door";
  c1.online = true;
  c1.position = 0;
  c1.lastAction = "close";
  c2.deviceId = "demo-c2";
  c2.name = "Living room blinds";
  c2.online = true;
  c2.position = 65;
  c2.lastAction = "stop";
  g_store.replaceCovers({c1, c2});

  ThermostatInfo t;
  t.deviceId = "demo-t1";
  t.name = "Heating";
  t.online = true;
  t.hasPower = true;
  t.on = true;
  t.hasCurrent = true;
  t.current = 20.5f;
  t.hasTarget = true;
  t.target = 22;
  t.minT = 5;
  t.maxT = 30;
  t.step = 0.5f;
  t.heating = 1;
  t.mode = "manual";
  g_store.replaceThermostats({t});

  BinarySensorInfo d1, d2, m1, m2;
  d1.deviceId = "demo-v1";
  d1.name = "Front door";
  d1.kind = BinaryKind::Door;
  d1.online = true;
  d1.battery = 90;
  d2.deviceId = "demo-v2";
  d2.name = "Back door";
  d2.kind = BinaryKind::Door;
  d2.online = true;
  d2.active = true;
  d2.battery = 80;
  m1.deviceId = "demo-m1";
  m1.name = "Driveway";
  m1.kind = BinaryKind::Motion;
  m1.online = true;
  m1.active = true;
  m1.battery = 70;
  m2.deviceId = "demo-m2";
  m2.name = "Hallway motion";
  m2.kind = BinaryKind::Motion;
  m2.online = true;
  m2.battery = 15;
  m2.batteryLow = true;
  g_store.replaceBinaries({d1, d2, m1, m2});

  SmokeInfo sm;
  sm.deviceId = "demo-d1";
  sm.name = "Kitchen smoke detector";
  sm.online = true;
  sm.alarm = smoke;
  sm.battery = 90;
  g_store.replaceSmokes({sm});

  g_store.setCloudStatus(CloudKind::Tuya, LinkState::Ok);
  g_store.setCloudStatus(CloudKind::EWeLink, LinkState::Ok);

  std::vector<scenes::Scene> scs(3);
  scs[0].id = "demo1";
  scs[0].name = "Good night";
  for (const char* k : {"demo-l1", "demo-l2", "demo-l4", "demo-l5", "demo-e1"}) scs[0].actions.push_back({k, "off"});
  scs[1].id = "demo2";
  scs[1].name = "Movie night";
  scs[1].actions = {{"demo-l1", "off"}, {"demo-p2", "on"}, {"demo-c2", "close"}};
  scs[2].id = "demo3";
  scs[2].name = "Leaving home";
  scs[2].actions = {{"demo-l1", "off"}, {"demo-l2", "off"}, {"demo-l4", "off"}, {"demo-t1", "t17"}};
  scenes::setDemo(scs);

  // Oznake na ugrađenoj slici kuće (assets/demo-house.svg)
  auto at = [](const char* key, int16_t x, int16_t y) {
    settings::Position p;
    p.key = key;
    p.x = x;
    p.y = y;
    return p;
  };
  home_ui::showcase({at("demo-l2", 306, 234), at("demo-l3", 504, 234), at("demo-l1", 307, 343),
                     at("demo-l4", 540, 341), at("demo-l5", 442, 290), at("demo-e1", 300, 440),
                     at("demo-c1", 662, 359), at("demo-s1", 95, 150), at("demo-t1", 705, 215),
                     at("demo-v1", 442, 372), at("demo-m1", 690, 440), at("demo-d1", 405, 150)});
  Serial.printf("@OK show%s\n", smoke ? " dim" : "");
}

// Prepoznavanje uređaja na primerima Tuya odgovora (bez pravih uređaja i bez mreže)
static void parseSamples() {
  static const char* const SAMPLES[] = {
      R"({"id":"demo-wk","name":"Termostat wk","online":true,"category":"wk","status":[{"code":"switch","value":true},{"code":"temp_set","value":220},{"code":"temp_current","value":215},{"code":"mode","value":"manual"},{"code":"work_state","value":"heating"},{"code":"child_lock","value":false}]})",
      R"({"id":"demo-wkf","name":"Glava radijatora wkf","online":true,"category":"wkf","status":[{"code":"mode","value":"auto"},{"code":"temp_set","value":21},{"code":"temp_current","value":19},{"code":"valve_state","value":"close"},{"code":"battery_percentage","value":55},{"code":"window_check","value":true}]})",
      R"({"id":"demo-kt","name":"Klima kt","online":true,"category":"kt","status":[{"code":"switch","value":false},{"code":"temp_set","value":24},{"code":"temp_current","value":26},{"code":"mode","value":"cold"}]})",
      R"({"id":"demo-wkcz","name":"Uticnica sa termostatom wkcz","online":true,"category":"wkcz","status":[{"code":"switch_1","value":true},{"code":"temp_set","value":25},{"code":"temp_current","value":23}]})",
      R"({"id":"demo-mcs","name":"Vrata mcs","online":true,"category":"mcs","status":[{"code":"doorcontact_state","value":true},{"code":"battery_percentage","value":90},{"code":"temper_alarm","value":false}]})",
      R"({"id":"demo-pir","name":"Pokret pir","online":true,"category":"pir","status":[{"code":"pir","value":"pir"},{"code":"battery_state","value":"low"}]})",
      R"({"id":"demo-hps","name":"Prisustvo hps","online":false,"category":"hps","status":[{"code":"presence_state","value":"none"},{"code":"illuminance_value","value":120}]})",
      R"({"id":"demo-dj","name":"Svetlo sa senzorom dj","online":true,"category":"dj","status":[{"code":"switch_led","value":true},{"code":"pir","value":"none"}]})",
  };
  TuyaClient t("", "", "", TUYA_CATEGORIES);  // posebna instanca: ne dira podatke cloud taska
  for (const char* j : SAMPLES) t.debugParse(j);
  Serial.println("@OK tp");
}

static void handle(const String& cmd) {
  // Komanda = aktivnost (kao dodir): inače se lista / prozorče posle dugog mirovanja odmah zatvore
  if (cmd != "s" && cmd != "s2") {
    lv_display_trigger_activity(nullptr);
    consoleWake();
  }
  if (cmd == "s" || cmd == "s2") {
    screenshot(cmd == "s2" ? 2 : 1);
  } else if (cmd.startsWith("r ")) {
    const long i = cmd.substring(2).toInt();
    if (s_rle && i >= 0 && (size_t)i * CHUNK < s_rleLen) sendChunk("@R", i);
    else Serial.println("@ERR r");
  } else if (cmd == "d") {
    demo();
  } else if (cmd == "ht") {
    // Neuspeo HTTPS zahtev (nepostojeći server): proverava ponovni pokušaj posle reset() bez pada
    HttpsSession t("test");
    const HttpResult r = t.request("GET", "https://nepostojeci.invalid/", "", {});
    Serial.printf("@OK ht %d\n", r.status);
  } else if (cmd == "dl" || cmd.startsWith("dl ")) {
    cloud::requestSmokeLog(cmd.length() > 3 ? cmd.substring(3) : String());
    Serial.println("@OK dl");
  } else if (cmd == "ds" || cmd == "ds0") {
    demoSmoke(cmd == "ds");
  } else if (cmd == "h") {
    ui::goHome();
    Serial.println("@OK home");
  } else if (cmd.startsWith("al ")) {
    alarm_ui::open(cmd.substring(3));
    Serial.println("@OK alarm");
  } else if (cmd == "show" || cmd == "show d") {
    showcase(cmd == "show d");
  } else if (cmd.startsWith("gs ")) {
    ui::scrollList(cmd.substring(3).toInt());
    Serial.println("@OK lista pomerena");
  } else if (cmd == "dt") {
    demoNew();
  } else if (cmd == "dv") {
    demoToggle("demo-v1");
  } else if (cmd == "dm") {
    demoToggle("demo-m1");
  } else if (cmd.startsWith("th ")) {
    lv_display_trigger_activity(nullptr);  // kao dodir: prozorče se inače zatvara posle 30 s bez dodira
    thermo_ui::open(cmd.substring(3));
    Serial.println("@OK termostat");
  } else if (cmd == "tp") {
    parseSamples();
  } else if (cmd == "dc") {
    demoCover();
  } else if (cmd.startsWith("co ")) {
    lv_display_trigger_activity(nullptr);
    cover_ui::open(cmd.substring(3));
    Serial.println("@OK roletna");
  } else if (cmd.startsWith("t ")) {
    int x = 0, y = 0;
    if (sscanf(cmd.c_str() + 2, "%d %d", &x, &y) == 2) {
      inject(x, y, x, y, 150);
      Serial.printf("@OK tap %d %d\n", x, y);
    }
  } else if (cmd.startsWith("w ")) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (sscanf(cmd.c_str() + 2, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
      inject(x0, y0, x1, y1, 400);
      Serial.printf("@OK swipe %d %d -> %d %d\n", x0, y0, x1, y1);
    }
  } else if (cmd.startsWith("p ")) {
    settings_ui::openUnlocked(cmd.substring(2).toInt());
    Serial.println("@OK settings");
  } else if (cmd.startsWith("pclk ")) {
    const float mhz = cmd.substring(5).toFloat();
    const bool ok = mhz >= 6 && mhz <= 20 && display::setPclk((uint32_t)(mhz * 1e6f));
    Serial.printf(ok ? "@OK pclk %.1f MHz\n" : "@ERR pclk %.1f\n", mhz);
  } else if (cmd == "rf") {
    display::setRefreshLog(!display::refreshLog());
    Serial.printf("@OK rf %s\n", display::refreshLog() ? "ukljucen" : "iskljucen");
  } else if (cmd == "w") {
    Serial.printf("@WEB %s?pin=%s\n", portal::baseUrl().c_str(), settings::webPin().c_str());
  } else if (cmd == "g") {
    ui::openList();
    Serial.println("@OK lista");
  } else if (cmd == "e") {
    home_ui::setEditMode(true);
    Serial.println("@OK uredjivanje");
  } else if (cmd == "m") {
    settings_ui::close();
    Serial.println("@OK main");
  }
}

void poll() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_line.length()) handle(s_line);
      s_line = "";
    } else if (s_line.length() < 64) {
      s_line += c;
    }
  }
}

}  // namespace debug
