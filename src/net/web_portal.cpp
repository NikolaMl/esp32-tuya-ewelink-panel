#include "net/web_portal.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <freertos/semphr.h>

#include <atomic>

#include "config.h"
#include "model/device_store.h"
#include "model/scenes.h"
#include "model/settings.h"
#include "net/ewelink_client.h"
#include "net/web_app_html.h"
#include "ui/background.h"
#include "util/crypto.h"
#include "util/i18n.h"
#include "util/restart.h"

namespace portal {

static WebServer s_server(80);
static String s_state;            // jednokratni OAuth "state"
static uint32_t s_stateUntil = 0;  // millis()
static std::atomic<uint32_t> s_oauthSeq{0};
static volatile bool s_oauthOk = false;
static String s_oauthMsg;
static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static void lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(s_mutex); }

static constexpr uint32_t STATE_TTL_MS = 10 * 60 * 1000;

String baseUrl() { return "http://" + WiFi.localIP().toString() + "/"; }
String redirectUrl() { return "http://" + WiFi.localIP().toString() + "/ewelink/callback"; }

String newLoginUrl() {
  const String st = crypto::randomAlnum(16);
  lock();
  s_state = st;
  s_stateUntil = millis() + STATE_TTL_MS;
  unlock();
  return baseUrl() + "ewelink/login?s=" + st;
}

uint32_t oauthSeq() { return s_oauthSeq; }
bool oauthOk() { return s_oauthOk; }
String oauthMessage() {
  lock();
  String m = s_oauthMsg;
  unlock();
  return m;
}

static void setOAuthResult(bool ok, const String& msg) {
  lock();
  s_oauthMsg = msg;
  unlock();
  s_oauthOk = ok;
  s_oauthSeq++;
  Serial.printf("[web] eWeLink: %s\n", msg.c_str());
}

static bool stateValid(const String& st) {
  lock();
  const bool ok = s_state.length() && st == s_state && (int32_t)(s_stateUntil - millis()) > 0;
  unlock();
  return ok;
}

static void consumeState() {
  lock();
  s_state = "";
  unlock();
}

// ---------------------------------------------------------------- HTML

static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char* CSS =
    "body{font-family:system-ui,sans-serif;background:#0e1116;color:#e8ecf1;margin:0;padding:16px}"
    "main{max-width:560px;margin:auto}h1{font-size:24px}h2{font-size:18px;margin-top:28px}"
    "section{background:#1b2129;border-radius:14px;padding:16px;margin:12px 0}"
    "label{display:block;margin:10px 0 4px;color:#9aa5b1;font-size:14px}"
    "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #333c47;"
    "background:#0e1116;color:#e8ecf1;font-size:16px}"
    "button,.btn{display:inline-block;margin-top:14px;padding:10px 18px;border:0;border-radius:20px;"
    "background:#f6b93b;color:#231a00;font-weight:600;font-size:16px;text-decoration:none}"
    "code{background:#0e1116;padding:2px 6px;border-radius:6px;word-break:break-all}"
    ".ok{color:#3fb950}.err{color:#f85149}.muted{color:#7d8896;font-size:14px}";

static String page(const String& title, const String& body) {
  return String("<!doctype html><html lang=") + i18n::code(i18n::lang()) + "><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'><title>" +
         esc(title) + "</title><style>" + CSS + "</style></head><body><main>" + body + "</main></body></html>";
}


// ---------------------------------------------------------------- PIN

// PIN sa ekrana (nasumičan pri svakom pokretanju). Posle 10 pogrešnih: 60 s zaključano.
// Broje se samo RAZLIČITI pogrešni PIN-ovi: stranica otvorena pre restarta panela svakih 15 s
// ponavlja isti stari PIN; to nije pogađanje i ne sme da zaključa web i za tačan PIN.
static uint8_t s_pinFails = 0;
static uint32_t s_pinLockUntil = 0;
static String s_wrongPins[4] = {"#", "#", "#", "#"};  // poslednji pogrešni ("#" = prazno mesto)
static uint8_t s_wrongNext = 0;

static uint32_t pinLockLeftMs() {
  if (!s_pinLockUntil) return 0;
  const int32_t left = (int32_t)(s_pinLockUntil - millis());
  if (left > 0) return left;
  s_pinLockUntil = 0;
  return 0;
}

static bool pinOk() {
  if (pinLockLeftMs()) return false;
  const String pin = s_server.arg("pin");
  if (pin == settings::webPin()) {
    s_pinFails = 0;
    return true;
  }
  for (const String& w : s_wrongPins)
    if (w == pin) return false;  // isti pogrešan PIN ponovo (stara stranica): ne broji se
  s_wrongPins[s_wrongNext] = pin;
  s_wrongNext = (s_wrongNext + 1) % 4;
  if (++s_pinFails >= 10) {
    s_pinFails = 0;
    s_pinLockUntil = millis() + 60000;
    Serial.println("[web] previše pogrešnih PIN-ova, 60 s zaključano");
  }
  return false;
}

static String lockText() {
  return TR("Previše pogrešnih PIN-ova. Sačekaj ", "Too many wrong PINs. Wait ") + String((pinLockLeftMs() + 999) / 1000) +
         TR(" s pa probaj ponovo.", " s and try again.");
}

static bool checkPin() {  // za HTML stranice
  if (pinOk()) return true;
  const bool locked = pinLockLeftMs() > 0;
  s_server.send(locked ? 429 : 403, "text/html; charset=utf-8",
                page(TR("Pogrešan PIN", "Wrong PIN"),
                     "<h1>" + String(locked ? TR("Zaključano", "Locked") : TR("Pogrešan PIN", "Wrong PIN")) + "</h1><p>" +
                         (locked ? lockText() : String(TR("PIN je prikazan na panelu: Podešavanja → Web.",
                                                           "The PIN is shown on the panel: Settings → Web."))) +
                         "</p><a class=btn href='/'>" + TR("Nazad", "Back") + "</a>"));
  return false;
}

static bool apiPin() {  // za /api/*
  if (pinOk()) return true;
  if (pinLockLeftMs()) s_server.send(429, "text/plain; charset=utf-8", lockText());
  else s_server.send(403, "text/plain; charset=utf-8", TR("Pogrešan PIN", "Wrong PIN"));
  return false;
}

static void sendJson(const JsonDocument& d) {
  String out;
  serializeJson(d, out);
  s_server.send(200, "application/json", out);
}

static void sendOk() { s_server.send(200, "application/json", "{\"ok\":true}"); }

static bool readBody(JsonDocument& b) {
  if (deserializeJson(b, s_server.arg("plain"))) {
    s_server.send(400, "text/plain; charset=utf-8", TR("Neispravan zahtev", "Invalid request"));
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- aplikacija

// Sačuvan jezik ide u <html lang="..">, da aplikacija odmah crta na pravom jeziku. HTML se ne kopira:
// šalje se novi početak, pa ostatak pravo iz flash-a.
static void handleApp() {
  static const char HEAD[] = "<!doctype html>\n<html lang=\"sr\">";
  const size_t skip = strncmp(WEB_APP_HTML, HEAD, sizeof(HEAD) - 1) == 0 ? sizeof(HEAD) - 1 : 0;
  String head;
  if (skip) head = String("<!doctype html>\n<html lang=\"") + i18n::code(settings::language()) + "\">";
  const size_t rest = sizeof(WEB_APP_HTML) - 1 - skip;
  s_server.setContentLength(head.length() + rest);
  s_server.send(200, "text/html; charset=utf-8", head);
  s_server.sendContent(WEB_APP_HTML + skip, rest);
}

static const char* linkStr(LinkState s) {
  switch (s) {
    case LinkState::Ok: return "ok";
    case LinkState::Connecting: return "connecting";
    case LinkState::Error: return "error";
    default: return "disabled";
  }
}

static void handleState() {
  if (!apiPin()) return;
  const Settings st = settings::get();
  const SystemStatus sys = g_store.systemStatus();
  const settings::ScreenCfg sc = settings::screen();
  const std::vector<settings::Position> pos = settings::positions();

  JsonDocument d;
  d["lang"] = i18n::code(settings::language());  // sačuvan izbor (važi posle restarta)
  d["ip"] = WiFi.localIP().toString();
  d["wifi"] = st.wifiSsid;
  for (int i = 0; i < 2; i++) {
    const CloudStatus& c = i == 0 ? sys.tuya : sys.ewelink;
    JsonObject o = d[i == 0 ? "tuya" : "ewelink"].to<JsonObject>();
    o["state"] = linkStr(c.state);
    o["msg"] = c.message;
  }
  d["redirect"] = redirectUrl();
  JsonObject so = d["screen"].to<JsonObject>();
  so["dimSec"] = sc.dimSec;
  so["offSec"] = sc.offSec;
  // Tajni ključevi se nikad ne šalju nazad — samo da li su sačuvani
  JsonObject co = d["cfg"].to<JsonObject>();
  co["tuyaEp"] = st.tuyaEndpoint;
  co["tuyaId"] = st.tuyaId;
  co["tuyaSecret"] = st.tuyaSecret.length() > 0;
  co["ewId"] = st.ewAppId;
  co["ewSecret"] = st.ewAppSecret.length() > 0;
  JsonArray regions = d["regions"].to<JsonArray>();
  for (size_t i = 0; i < TUYA_REGION_COUNT; i++) {
    JsonObject r = regions.add<JsonObject>();
    r["l"] = TUYA_REGIONS[i].label;
    r["e"] = TUYA_REGIONS[i].endpoint;
  }

  auto item = [&](JsonArray arr, const String& key, const String& name, const char* type, bool online) {
    JsonObject o = arr.add<JsonObject>();
    o["k"] = key;
    o["n"] = name;
    o["t"] = type;
    o["h"] = settings::isHidden(key);
    o["o"] = online;
    o["c"] = "t";  // izvor: t = Tuya, e = eWeLink, p = napravljeno na panelu (scene)
    return o;
  };
  // Raspored: svaka oznaka na slici posebno (isti uređaj može više puta)
  JsonArray lay = d["layout"].to<JsonArray>();
  for (const settings::Position& p : pos) {
    JsonObject o = lay.add<JsonObject>();
    o["k"] = p.key;
    o["x"] = p.x;
    o["y"] = p.y;
    if (p.style != settings::STYLE_BUTTON) o["s"] = p.style;
    if (p.page) o["g"] = p.page;
  }
  // Delovi početnog ekrana: naziv i da li ima sliku
  JsonArray pages = d["pages"].to<JsonArray>();
  const std::vector<String> names = settings::pageNames();
  for (size_t i = 0; i < names.size(); i++) {
    JsonObject o = pages.add<JsonObject>();
    o["n"] = names[i];
    o["img"] = background::hasImage(i);
    o["own"] = background::hasCustom(i);
  }
  JsonArray items = d["items"].to<JsonArray>();
  for (const AlarmState& a : g_store.alarms()) item(items, a.key(), a.name, "a", a.online);
  for (const SensorInfo& x : g_store.sensors()) {
    char v[16] = "--";
    if (x.hasValue) {
      snprintf(v, sizeof(v), "%.1f°", x.value);
      if (char* dot = strchr(v, '.'); dot && !i18n::en()) *dot = ',';  // decimalni zarez na srpskom
    }
    item(items, x.key(), x.name, "s", x.online)["v"] = v;
  }
  for (const SwitchState& l : g_store.snapshot()) {
    JsonObject o = item(items, l.key(), l.name, "l", l.online);
    o["on"] = l.on;
    o["grp"] = groupId(l.group);
    if (l.cloud == CloudKind::EWeLink) o["c"] = "e";
  }
  // Scene: stavke za raspored (dugme na slici) i lista sa akcijama za uređivanje
  JsonArray scArr = d["scenes"].to<JsonArray>();
  for (const scenes::Scene& s : scenes::all()) {
    JsonObject so = item(items, scenes::keyFor(s.id), s.name, "x", true);
    so["v"] = String(s.actions.size()) + (i18n::en() ? (s.actions.size() == 1 ? " device" : " devices") : " uređ.");
    so["c"] = "p";
    JsonObject o = scArr.add<JsonObject>();
    o["id"] = s.id;
    o["n"] = s.name;
    JsonArray a = o["a"].to<JsonArray>();
    for (const scenes::Action& x : s.actions) {
      JsonObject j = a.add<JsonObject>();
      j["k"] = x.key;
      j["o"] = x.op;
    }
  }
  for (const SmokeInfo& x : g_store.smokes()) {
    JsonObject o = item(items, x.key(), x.name, "d", x.online);
    o["v"] = smokeStateText(x);
    o["al"] = x.online && x.alarm;
  }
  for (const CoverState& c : g_store.covers()) {
    JsonObject o = item(items, c.key(), c.name, "c", c.online);
    if (c.position >= 0) o["v"] = String(c.position) + "%";
  }
  // Termostat: v = "21,5° → 22°", raspon i korak za izbor temperature u scenama
  for (const ThermostatState& t : g_store.thermostats()) {
    JsonObject o = item(items, t.key(), t.name, "t", t.online);
    o["v"] = thermoStateText(t);
    o["on"] = t.on;
    o["pw"] = t.hasPower;
    o["mn"] = t.minT;
    o["mx"] = t.maxT;
    o["st"] = t.step;
    if (t.hasTarget) o["tg"] = t.target;
  }
  // Vrata ("v") i pokret ("m"): al = otvoreno / pokret
  for (const BinarySensorState& b : g_store.binaries()) {
    JsonObject o = item(items, b.key(), b.name, b.kind == BinaryKind::Door ? "v" : "m", b.online);
    String v = binaryStateText(b);
    const String note = binaryNote(b);
    if (note.length()) v += " · " + note;
    o["v"] = v;
    o["al"] = b.online && b.active;
  }
  sendJson(d);
}

static void handleLayout() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  if (b["reset"] | false) {
    settings::resetPositions();
    sendOk();
    return;
  }
  std::vector<settings::Position> list;
  for (JsonObject p : b["p"].as<JsonArray>()) {
    if (list.size() >= settings::MAX_POSITIONS) break;
    const String key = p["k"] | "";
    if (key.isEmpty() || key.length() > 96) continue;
    settings::Position pos{key, (int16_t)constrain(p["x"] | 0, 0, 800), (int16_t)constrain(p["y"] | 0, 0, 480)};
    pos.style = (uint8_t)constrain(p["s"] | 0, 0, (int)settings::STYLE_INVISIBLE);
    pos.page = (uint8_t)constrain(p["g"] | 0, 0, settings::pageCount() - 1);
    list.push_back(pos);
  }
  settings::replacePositions(list);
  Serial.printf("[web] raspored: %u oznaka\n", (unsigned)list.size());
  sendOk();
}

static void handleHidden() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  const String key = b["k"] | "";
  if (key.isEmpty()) return s_server.send(400, "text/plain", "k");
  settings::setHidden(key, b["h"] | false);
  sendOk();
}

static void handleScreen() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  settings::ScreenCfg c;
  c.dimSec = constrain(b["dimSec"] | 15, 5, 3600);
  c.offSec = constrain(b["offSec"] | 20, 0, 36000);
  settings::setScreen(c);
  sendOk();
}

static void handleTuya() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  const Settings st = settings::get();
  String ep = b["ep"] | "", id = b["id"] | "", secret = b["secret"] | "";
  bool known = false;
  for (size_t i = 0; i < TUYA_REGION_COUNT; i++) known |= ep == TUYA_REGIONS[i].endpoint;
  if (!known) return s_server.send(400, "text/plain; charset=utf-8", TR("Nepoznat data centar", "Unknown data center"));
  id.trim();
  secret.trim();
  settings::setTuya(ep, id, secret.length() ? secret : st.tuyaSecret);
  sendOk();
}

static void handleEWeLink() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  const Settings st = settings::get();
  String id = b["id"] | "", secret = b["secret"] | "";
  id.trim();
  secret.trim();
  settings::setEWeLink(id, secret.length() ? secret : st.ewAppSecret);
  sendOk();
}

static void handleWifi() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  String ssid = b["ssid"] | "";
  ssid.trim();
  if (ssid.isEmpty()) return s_server.send(400, "text/plain; charset=utf-8", TR("Unesi naziv mreže", "Enter the network name"));
  settings::setWifi(ssid, b["pass"] | "");
  sendOk();
}

// ---------------------------------------------------------------- pozadina

// Deo početnog ekrana iz upita (?page=N); -1 = nepostojeći deo
static int pageArg() {
  const int page = s_server.hasArg("page") ? s_server.arg("page").toInt() : 0;
  return page >= 0 && page < settings::pageCount() ? page : -1;
}

static void handleBgGet() {
  if (!apiPin()) return;
  const int page = pageArg();
  if (page < 0 || !background::hasImage(page)) return s_server.send(404, "text/plain; charset=utf-8", TR("nema slike", "no photo"));
  s_server.setContentLength(background::SIZE);
  s_server.send(200, "application/octet-stream", "");
  background::stream(page, [](const uint8_t* d, size_t n) { s_server.sendContent(reinterpret_cast<const char*>(d), n); });
}

static File s_up;
static size_t s_upSize = 0;
static int s_upPage = 0;
static bool s_upOk = false, s_upPin = false;

static void onBgUpload() {
  HTTPUpload& u = s_server.upload();
  if (u.status == UPLOAD_FILE_START) {
    s_upPin = pinOk();
    s_upSize = 0;
    s_upPage = pageArg();
    s_upOk = s_upPin && s_upPage >= 0 && background::fsReady();
    if (s_upOk) s_up = LittleFS.open(background::TMP_PATH, "w");
    s_upOk = s_upOk && s_up;
    Serial.printf("[web] prijem pozadine (deo %d): %s\n", s_upPage, s_upOk ? "počinje" : "odbijeno");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (s_upOk && (s_upSize + u.currentSize > background::SIZE || s_up.write(u.buf, u.currentSize) != u.currentSize))
      s_upOk = false;
    s_upSize += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (s_up) s_up.close();
    if (u.status == UPLOAD_FILE_END && s_upOk && s_upSize == background::SIZE) {
      s_upOk = background::commitUpload(s_upPage);
    } else {
      s_upOk = false;
    }
    if (!s_upOk && background::fsReady()) LittleFS.remove(background::TMP_PATH);
    Serial.printf("[web] pozadina: %u B, %s\n", (unsigned)s_upSize, s_upOk ? "sačuvana" : "greška");
  }
}

static void handleBgDone() {
  if (!s_upPin) return s_server.send(403, "text/plain; charset=utf-8", TR("Pogrešan PIN", "Wrong PIN"));
  if (!s_upOk)
    return s_server.send(400, "text/plain; charset=utf-8",
                         TR("Slika nije sačuvana (nema mesta ili prekinuta veza)", "Photo not saved (no space left or connection lost)"));
  background::markChanged();
  settings::touchUi();
  sendOk();
}

static void handleBgReset() {
  if (!apiPin()) return;
  const int page = pageArg();
  if (page < 0) return s_server.send(400, "text/plain; charset=utf-8", TR("Nepostojeći deo", "No such page"));
  if (!background::removeCustom(page)) return s_server.send(500, "text/plain; charset=utf-8", TR("Brisanje nije uspelo", "Delete failed"));
  background::markChanged();
  settings::touchUi();
  sendOk();
}

// Scene: {scenes:[{id?, n, a:[{k, o}]}]} = zameni sve. Nova scena bez id dobija nasumičan.
static void handleScenes() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  static const char* const OPS[] = {"on", "off", "open", "close", "stop", "away", "home", "disarm"};
  std::vector<scenes::Scene> list;
  for (JsonObject o : b["scenes"].as<JsonArray>()) {
    if (list.size() >= scenes::MAX_SCENES) break;
    scenes::Scene s;
    s.id = o["id"] | "";
    if (s.id.isEmpty() || s.id.length() > 12) s.id = crypto::randomAlnum(6);
    s.name = String(o["n"] | "").substring(0, 32);
    s.name.trim();
    if (s.name.isEmpty()) s.name = TR("Scena", "Scene");
    for (JsonObject a : o["a"].as<JsonArray>()) {
      if (s.actions.size() >= scenes::MAX_ACTIONS) break;
      const String k = a["k"] | "", op = a["o"] | "";
      bool okOp = false;
      for (const char* x : OPS) okOp = okOp || op == x;
      if (op.length() >= 2 && op.length() <= 6 && op[0] == 't') {  // termostat: "t21.5"
        const float v = op.substring(1).toFloat();
        okOp = v >= 4 && v <= 40;
      }
      if (k.length() && k.length() <= 96 && okOp) s.actions.push_back({k, op});
    }
    list.push_back(s);
  }
  if (!scenes::save(list)) return s_server.send(500, "text/plain; charset=utf-8", TR("Scene nisu sačuvane", "Scenes not saved"));
  // Dugmad obrisanih scena nestaju i sa slike
  std::vector<settings::Position> pos = settings::positions();
  const size_t before = pos.size();
  pos.erase(std::remove_if(pos.begin(), pos.end(), [&](const settings::Position& p) {
              if (!scenes::isSceneKey(p.key)) return false;
              for (const scenes::Scene& s : list)
                if (s.id == scenes::idOf(p.key)) return false;
              return true;
            }),
            pos.end());
  if (pos.size() != before) settings::replacePositions(pos);
  settings::touchUi();
  Serial.printf("[web] scene: %u\n", (unsigned)list.size());
  sendOk();
}

static void handleSceneRun() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  scenes::Scene s;
  if (!scenes::find(b["id"] | "", s)) return s_server.send(404, "text/plain; charset=utf-8", TR("Nema te scene", "No such scene"));
  scenes::requestRun(s.id);  // izvršava UI task (PIN na panelu ako treba isključiti alarm)
  sendOk();
}

// Delovi početnog ekrana: {op:"add", name} | {op:"del", i} | {op:"names", names:[...]}
static void handlePages() {
  if (!apiPin()) return;
  JsonDocument b;
  if (!readBody(b)) return;
  const String op = b["op"] | "";
  if (op == "add") {
    if (!settings::addPage(b["name"] | "")) return s_server.send(400, "text/plain; charset=utf-8", TR("Najviše ", "At most ") + String(HOME_MAX_PAGES) + TR(" delova", " pages"));
  } else if (op == "del") {
    const int i = b["i"] | -1;
    const uint8_t n = settings::pageCount();
    if (n < 2 || i < 0 || i >= n) return s_server.send(400, "text/plain; charset=utf-8", TR("Ne može se obrisati", "This page can't be deleted"));
    background::deletePage(i, n);
    settings::deletePage(i);
    background::markChanged();
  } else if (op == "names") {
    std::vector<String> names;
    for (JsonVariant v : b["names"].as<JsonArray>()) names.push_back(v | "");
    settings::renamePages(names);
  } else {
    return s_server.send(400, "text/plain; charset=utf-8", TR("Nepoznata radnja", "Unknown action"));
  }
  Serial.printf("[web] delovi: %s -> %u\n", op.c_str(), (unsigned)settings::pageCount());
  sendOk();
}

// Jezik panela i aplikacije: {"lang":"en"|"sr"} (ili ?lang=). Promena se primenjuje restartom panela;
// web PIN ostaje isti posle softverskog restarta, pa telefon nastavlja bez ponovnog skeniranja.
static void handleLang() {
  if (!apiPin()) return;
  String code = s_server.hasArg("lang") ? s_server.arg("lang") : String();
  if (code.isEmpty()) {
    JsonDocument b;
    if (!readBody(b)) return;
    code = b["lang"] | "";
  }
  Lang l = Lang::En;
  if (!i18n::parse(code.c_str(), l)) return s_server.send(400, "text/plain; charset=utf-8", TR("Nepoznat jezik", "Unknown language"));
  const bool changed = settings::setLanguage(l);
  if (changed) restart::soon(1500);
  Serial.printf("[web] jezik: %s%s\n", code.c_str(), changed ? " (restart)" : "");
  s_server.send(200, "application/json", changed ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true,\"restart\":false}");
}

static void sendOAuthRedirect(const String& state) {
  const Settings st = settings::get();
  if (!st.ewAppId.length() || !st.ewAppSecret.length()) {
    s_server.send(400, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1>") + TR("Prvo unesi App ID i App Secret", "Enter the App ID and App Secret first") +
                                      "</h1><a class=btn href='/'>" + TR("Nazad", "Back") + "</a>"));
    return;
  }
  if (time(nullptr) < 1700000000) {
    s_server.send(503, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1>") + TR("Panel još nema tačno vreme", "The panel doesn't have the correct time yet") +
                                      "</h1><p>" + TR("Sačekaj par sekundi pa probaj ponovo.", "Wait a few seconds and try again.") + "</p>"));
    return;
  }
  s_server.sendHeader("Location", EWeLinkClient::oauthUrl(st.ewAppId, st.ewAppSecret, redirectUrl(), state));
  s_server.send(302);
  Serial.printf("[web] eWeLink: browser poslat na eWeLink prijavu (povratak na %s)\n", redirectUrl().c_str());
}

// Link iz QR koda na ekranu
static void handleLogin() {
  const String st = s_server.arg("s");
  if (!stateValid(st)) {
    Serial.println("[web] eWeLink: link iz QR koda je istekao");
    s_server.send(410, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1>") + TR("Link je istekao", "This link has expired") + "</h1><p>" +
                                      TR("Na panelu ponovo pritisni „Poveži nalog“.", "Press “Link account” on the panel again.") + "</p>"));
    return;
  }
  sendOAuthRedirect(st);
}

// Pokretanje prijave sa web stranice (uz PIN)
static void handleStart() {
  if (!checkPin()) return;
  const String url = newLoginUrl();
  sendOAuthRedirect(url.substring(url.indexOf("s=") + 2));
}

static void handleCallback() {
  const String code = s_server.arg("code");
  const String region = s_server.arg("region");
  const String st = s_server.arg("state");
  // Sam kod se ne ispisuje (jednokratna lozinka za tokene)
  Serial.printf("[web] eWeLink povratak: kod %s, region '%s', state %s\n", code.length() ? "stigao" : "NEMA",
                region.c_str(), stateValid(st) ? "ok" : "NEISPRAVAN/istekao");
  if (!stateValid(st)) {
    s_server.send(400, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1>") + TR("Nevažeći ili istekao zahtev", "Invalid or expired request") + "</h1><p>" +
                                      TR("Ponovo pokreni povezivanje sa panela.", "Start linking again from the panel.") + "</p>"));
    return;
  }
  consumeState();
  if (!code.length()) {
    const char* msg = TR("eWeLink nije vratio kod", "eWeLink returned no code");
    setOAuthResult(false, msg);
    s_server.send(400, "text/html; charset=utf-8", page("eWeLink", String("<h1>") + msg + "</h1>"));
    return;
  }
  const Settings cfg = settings::get();
  String err;
  // "code" važi samo 30 s — menjamo ga odmah
  if (EWeLinkClient::exchangeCode(cfg.ewAppId, cfg.ewAppSecret, code, region.length() ? region : "eu",
                                  redirectUrl(), err)) {
    settings::touchEWeLinkTokens();
    setOAuthResult(true, TR("eWeLink nalog je povezan (region ", "eWeLink account linked (region ") + region + ")");
    s_server.send(200, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1 class=ok>") + TR("Povezano!", "Linked!") + "</h1><p>" +
                                      TR("Panel sada učitava tvoje eWeLink prekidače. Možeš da zatvoriš ovu stranicu.",
                                         "The panel is now loading your eWeLink switches. You can close this page.") +
                                      "</p>"));
  } else {
    setOAuthResult(false, TR("Prijava nije uspela: ", "Sign-in failed: ") + err);
    s_server.send(502, "text/html; charset=utf-8",
                  page("eWeLink", String("<h1 class=err>") + TR("Prijava nije uspela", "Sign-in failed") + "</h1><p>" + esc(err) + "</p>"));
  }
}

static void task(void*) {
  s_server.on("/", HTTP_GET, handleApp);
  s_server.on("/api/state", HTTP_GET, handleState);
  s_server.on("/api/layout", HTTP_POST, handleLayout);
  s_server.on("/api/hidden", HTTP_POST, handleHidden);
  s_server.on("/api/screen", HTTP_POST, handleScreen);
  s_server.on("/api/tuya", HTTP_POST, handleTuya);
  s_server.on("/api/ewelink", HTTP_POST, handleEWeLink);
  s_server.on("/api/wifi", HTTP_POST, handleWifi);
  s_server.on("/api/bg", HTTP_GET, handleBgGet);
  s_server.on("/api/bg", HTTP_POST, handleBgDone, onBgUpload);
  s_server.on("/api/bg/reset", HTTP_POST, handleBgReset);
  s_server.on("/api/pages", HTTP_POST, handlePages);
  s_server.on("/api/scenes", HTTP_POST, handleScenes);
  s_server.on("/api/scenes/run", HTTP_POST, handleSceneRun);
  s_server.on("/api/lang", HTTP_POST, handleLang);
  s_server.on("/ewelink/start", HTTP_GET, handleStart);
  s_server.on("/ewelink/login", HTTP_GET, handleLogin);
  s_server.on("/ewelink/callback", HTTP_GET, handleCallback);
  s_server.onNotFound([] { s_server.send(404, "text/plain", "404"); });
  s_server.begin();
  for (;;) {
    s_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void begin() { xTaskCreatePinnedToCore(task, "web", 16 * 1024, nullptr, 2, nullptr, 0); }

}  // namespace portal
