#include "net/tuya_client.h"

#include <mbedtls/base64.h>
#include <time.h>

#include <sys/time.h>

#include <algorithm>

#include "model/settings.h"
#include "util/crypto.h"
#include "util/i18n.h"

uint64_t epochMillis() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static String u64ToString(uint64_t v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  return String(buf);
}

TuyaClient::TuyaClient(const char* endpoint, const char* accessId, const char* accessSecret,
                       const char* categories)
    : endpoint_(endpoint), accessId_(accessId), secret_(accessSecret), categories_(categories) {
  while (endpoint_.endsWith("/")) endpoint_.remove(endpoint_.length() - 1);
  categories_ = "," + categories_ + ",";
  categories_.replace(" ", "");
}

bool TuyaClient::configured() const { return accessId_.length() > 0 && secret_.length() > 0; }

// ---------------------------------------------------------------- potpis

String TuyaClient::sortedQuery(QueryList q, bool encode) {
  std::sort(q.begin(), q.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  String s;
  for (const auto& kv : q) {
    if (s.length()) s += '&';
    s += kv.first;
    s += '=';
    s += encode ? crypto::urlEncode(kv.second) : kv.second;
  }
  return s;
}

String TuyaClient::buildStringToSign(const String& method, const String& body, const String& pathWithQuery) {
  // HTTPMethod \n Content-SHA256 \n Signature-Headers (prazno) \n URL
  return method + "\n" + crypto::sha256Hex(body) + "\n" + "\n" + pathWithQuery;
}

String TuyaClient::buildSign(const String& accessId, const String& secret, const String& token, const String& t,
                             const String& stringToSign) {
  // str = client_id + access_token + t + nonce + stringToSign  (nonce ne koristimo)
  return crypto::hmacSha256Hex(secret, accessId + token + t + stringToSign, true);
}

// ---------------------------------------------------------------- HTTP

String TuyaClient::describeError(int code, const String& msg) {
  String hint;
  switch (code) {
    case 1004: hint = TR("pogrešan Access Secret (sign invalid)", "wrong Access Secret (sign invalid)"); break;
    case 1013: hint = TR("sat nije tačan (NTP)", "clock is wrong (NTP)"); break;
    case 1106: hint = TR("nema dozvole - proveri da je Smart Life nalog povezan sa projektom",
                         "no permission - check that the Smart Life account is linked to the project"); break;
    case 2008: hint = TR("komanda nije podržana za ovaj uređaj", "command not supported by this device"); break;
    case 28841002: hint = TR("Tuya trial je istekao - produži ga na iot.tuya.com", "Tuya trial expired - extend it at iot.tuya.com"); break;
    case 28841101: hint = TR("API nije pretplaćen - dodaj IoT Core servis projektu", "API not subscribed - add the IoT Core service to the project"); break;
    case 28841105: hint = TR("projekat nema dozvolu - proveri data centar i IoT Core", "project has no permission - check the data center and IoT Core"); break;
    default: break;
  }
  String s = "Tuya " + String(code) + ": " + (hint.length() ? hint : msg);
  return s;
}

bool TuyaClient::ensureToken(String& err) {
  if (token_.length() && (int32_t)(tokenValidUntil_ - millis()) > 60000) return true;
  token_ = "";

  const String path = "/v1.0/token";
  const QueryList q = {{"grant_type", "1"}};
  const String pq = path + "?" + sortedQuery(q, false);
  const String t = u64ToString(epochMillis());
  const String sign = buildSign(accessId_, secret_, "", t, buildStringToSign("GET", "", pq));

  HeaderList h = {{"client_id", accessId_}, {"sign", sign}, {"t", t}, {"sign_method", "HMAC-SHA256"}};
  HttpResult r = http_.request("GET", endpoint_ + path + "?" + sortedQuery(q, true), "", h);
  if (!r.ok()) {
    err = r.status < 0 ? TR("Tuya: nema veze sa serverom (", "Tuya: no connection to the server (") + HTTPClient::errorToString(r.status) + ")"
                       : "Tuya: HTTP " + String(r.status);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    err = TR("Tuya: neispravan odgovor za token", "Tuya: invalid token response");
    return false;
  }
  if (!doc["success"].as<bool>()) {
    err = describeError(doc["code"] | 0, doc["msg"] | "");
    return false;
  }
  token_ = doc["result"]["access_token"].as<String>();
  const uint32_t expireSec = doc["result"]["expire_time"] | 3600;
  tokenValidUntil_ = millis() + expireSec * 1000UL;
  log_i("[tuya] novi token, vazi %u s", (unsigned)expireSec);
  return token_.length() > 0;
}

bool TuyaClient::call(const char* method, const String& path, const QueryList& query, const String& body,
                      JsonDocument& doc, const JsonDocument* filter, String& err, bool allowRetry) {
  if (!ensureToken(err)) return false;

  const String pqSign = query.empty() ? path : path + "?" + sortedQuery(query, false);
  const String pqUrl = query.empty() ? path : path + "?" + sortedQuery(query, true);
  const String t = u64ToString(epochMillis());
  const String sign = buildSign(accessId_, secret_, token_, t, buildStringToSign(method, body, pqSign));

  HeaderList h = {{"client_id", accessId_}, {"access_token", token_}, {"sign", sign},
                  {"t", t},                 {"sign_method", "HMAC-SHA256"}};
  if (body.length()) h.push_back({"Content-Type", "application/json"});

  HttpResult r = http_.request(method, endpoint_ + pqUrl, body, h);
  if (!r.ok()) {
    err = r.status < 0 ? TR("Tuya: nema veze sa serverom (", "Tuya: no connection to the server (") + HTTPClient::errorToString(r.status) + ")"
                       : "Tuya: HTTP " + String(r.status);
    return false;
  }

  doc.clear();
  DeserializationError de = filter ? deserializeJson(doc, r.body, DeserializationOption::Filter(*filter))
                                   : deserializeJson(doc, r.body);
  if (de) {
    err = TR("Tuya: neispravan JSON (", "Tuya: invalid JSON (") + String(de.c_str()) + ")";
    return false;
  }
  if (!doc["success"].as<bool>()) {
    const int code = doc["code"] | 0;
    if ((code == 1010 || code == 1011) && allowRetry) {  // token nevažeći / istekao
      token_ = "";
      return call(method, path, query, body, doc, filter, err, false);
    }
    err = describeError(code, doc["msg"] | "");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- uređaji

bool TuyaClient::categoryAllowed(const String& cat) const {
  if (categories_ == ",," || categories_ == ",*,") return true;
  return categories_.indexOf("," + cat + ",") >= 0;
}

// "switch", "switch_led", "switch_1", "switch_led_2" -> prekidač svetla.
// "switch_backlight", "switch_usb1", "switch_inching" itd. -> ne.
static bool isSwitchCode(const String& code, String& suffix) {
  suffix = "";
  if (code == "switch" || code == "switch_led") return true;
  String rest;
  if (code.startsWith("switch_led_")) rest = code.substring(11);
  else if (code.startsWith("switch_")) rest = code.substring(7);
  else return false;
  if (rest.length() == 0 || rest.length() > 2) return false;
  for (size_t i = 0; i < rest.length(); i++)
    if (!isdigit(static_cast<unsigned char>(rest[i]))) return false;
  suffix = rest;
  return true;
}

// Jedan uređaj iz liste: alarm, roletna, dim, termostat, vrata/pokret, temperatura ili prekidač
void TuyaClient::parseDevice(JsonObject dev, std::vector<SwitchInfo>& out) {
  const String category = dev["category"] | "";
  if (category == "mal") {
    parseAlarm(dev);
    return;
  }
  if (parseCover(dev, category)) return;
  if (parseSmoke(dev, category)) return;
  if (parseThermostat(dev, category)) return;  // pre senzora: termostat javlja i trenutnu temperaturu
  if (parseBinary(dev, category)) return;
  const size_t nSensors = sensors_.size();
  parseSensor(dev);
  if (!categoryAllowed(category)) {
    if (sensors_.size() == nSensors) logUnknown(dev, category);
    return;
  }

  struct Ch { String code, suffix; bool on; };
  std::vector<Ch> chans;
  for (JsonObject st : dev["status"].as<JsonArray>()) {
    const String code = st["code"] | "";
    String suffix;
    if (!st["value"].is<bool>() || !isSwitchCode(code, suffix)) continue;
    chans.push_back({code, suffix, st["value"].as<bool>()});
  }
  if (chans.empty()) return;

  const String id = dev["id"] | "";
  const String devName = dev["name"] | id;
  const bool online = dev["online"] | false;
  const bool multi = chans.size() > 1;
  for (const Ch& c : chans) {
    SwitchInfo s;
    s.cloud = CloudKind::Tuya;
    s.deviceId = id;
    s.channel = c.code;
    s.isMultiChannel = multi;
    s.name = multi && c.suffix.length() ? devName + " " + c.suffix : devName;
    s.on = c.on;
    s.online = online;
    s.group = groupForTuyaCategory(category);
    out.push_back(s);
  }
}

bool TuyaClient::fetch(std::vector<SwitchInfo>& out, String& err) {
  JsonDocument filter;
  filter["success"] = true;
  filter["code"] = true;
  filter["msg"] = true;
  filter["result"]["has_more"] = true;
  filter["result"]["last_row_key"] = true;
  JsonObject fd = filter["result"]["devices"].add<JsonObject>();
  fd["id"] = true;
  fd["name"] = true;
  fd["online"] = true;
  fd["category"] = true;
  fd["status"] = true;

  out.clear();
  alarms_.clear();
  sensors_.clear();
  covers_.clear();
  smokes_.clear();
  thermostats_.clear();
  binaries_.clear();
  String lastRowKey;
  for (int page = 0; page < 10; page++) {
    QueryList q = {{"size", "100"}};
    if (lastRowKey.length()) q.push_back({"last_row_key", lastRowKey});

    JsonDocument doc;
    if (!call("GET", "/v1.0/iot-01/associated-users/devices", q, "", doc, &filter, err)) return false;

    JsonObject result = doc["result"];
    for (JsonObject dev : result["devices"].as<JsonArray>()) {
      parseDevice(dev, out);
    }

    if (!(result["has_more"] | false)) break;
    lastRowKey = result["last_row_key"] | "";
    if (!lastRowKey.length()) break;
  }
  return true;
}

// alarm_msg: base64 UTF-16BE tekst ("System Alarm: Door/Window (03 Zone)(Otvor - sredina)")
static String decodeAlarmMsg(const String& b64) {
  size_t n = 0;
  uint8_t buf[256];
  if (mbedtls_base64_decode(buf, sizeof(buf), &n, (const uint8_t*)b64.c_str(), b64.length()) != 0) return "";
  String out;
  for (size_t i = 0; i + 1 < n; i += 2) {
    const uint16_t c = (buf[i] << 8) | buf[i + 1];
    if (c == 0) break;
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
  }
  out.replace("\n", " ");
  out.trim();
  return out;
}

// Iz statusa uzimamo samo ono što panel prikazuje. Osetljivi DP-ovi (brojevi za dojavu,
// šifra tastature) se ne čitaju i ne loguju.
void TuyaClient::parseAlarm(JsonObject dev) {
  AlarmInfo a;
  a.deviceId = dev["id"] | "";
  a.name = dev["name"] | a.deviceId;
  a.online = dev["online"] | false;
  for (JsonObject st : dev["status"].as<JsonArray>()) {
    const String code = st["code"] | "";
    if (code == "master_mode") a.mode = alarmModeFromTuya(st["value"] | "");
    else if (code == "master_state") a.alarming = String(st["value"] | "") == "alarm";
    else if (code == "delay_set") a.exitDelay = st["value"] | 0;
    else if (code == "alarm_delay_time") a.entryDelay = st["value"] | 0;
    else if (code == "alarm_msg") a.lastMsg = decodeAlarmMsg(st["value"] | "");
  }
  if (a.mode == AlarmMode::Sos) a.alarming = true;
  alarms_.push_back(a);
}

// ---------------------------------------------------------------- roletne

static bool isControlValue(const String& v) {
  return v == "open" || v == "stop" || v == "close" || v == "continue";
}

// "control" / "control_2" -> "" / "_2"
static bool coverSuffix(const String& code, const char* base, String& suffix) {
  const size_t n = strlen(base);
  if (!code.startsWith(base)) return false;
  suffix = code.substring(n);
  return suffix.isEmpty() || (suffix.length() <= 2 && suffix[0] == '_' && isdigit((unsigned char)suffix[1]));
}

// Modul za roletne: kategorija "cl" / "clkg", ili bilo koji uređaj sa DP "control" = open/stop/close.
bool TuyaClient::parseCover(JsonObject dev, const String& category) {
  JsonArray status = dev["status"].as<JsonArray>();
  struct Ch {
    String suffix, last;
    int8_t pos = -1, posCtl = -1;
  };
  std::vector<Ch> chans;
  for (JsonObject st : status) {
    const String code = st["code"] | "";
    String sfx;
    if (coverSuffix(code, "control", sfx) && st["value"].is<const char*>() &&
        (isControlValue(st["value"].as<String>()) || category == "cl" || category == "clkg")) {
      Ch c;
      c.suffix = sfx;
      c.last = st["value"].as<String>();
      chans.push_back(c);
    }
  }
  if (chans.empty()) return false;
  for (JsonObject st : status) {
    const String code = st["code"] | "";
    String sfx;
    const bool isState = coverSuffix(code, "percent_state", sfx);
    if (!isState && !coverSuffix(code, "percent_control", sfx)) continue;
    if (!st["value"].is<int>()) continue;
    for (Ch& c : chans) {
      if (c.suffix != sfx) continue;
      const int8_t v = (int8_t)constrain(st["value"].as<int>(), 0, 100);
      if (isState) c.pos = v;
      else c.posCtl = v;
    }
  }
  const String id = dev["id"] | "";
  const String devName = dev["name"] | id;
  for (const Ch& c : chans) {
    CoverInfo ci;
    ci.deviceId = id;
    ci.controlCode = "control" + c.suffix;
    ci.isMultiChannel = chans.size() > 1;
    ci.name = ci.isMultiChannel && c.suffix.length() ? devName + " " + c.suffix.substring(1) : devName;
    ci.online = dev["online"] | false;
    ci.position = c.pos >= 0 ? c.pos : c.posCtl;  // percent_state = stvarni položaj, ako ga modul javlja
    ci.lastAction = c.last;
    covers_.push_back(ci);
  }
  return true;
}

// ---------------------------------------------------------------- detektori dima

bool TuyaClient::parseSmoke(JsonObject dev, const String& category) {
  if (category != "ywbj") return false;
  SmokeInfo s;
  s.deviceId = dev["id"] | "";
  s.name = dev["name"] | s.deviceId;
  s.online = dev["online"] | false;
  String dump;
  for (JsonObject st : dev["status"].as<JsonArray>()) {
    const String code = st["code"] | "";
    JsonVariant v = st["value"];
    dump += code + "=" + v.as<String>() + " ";
    if (code == "smoke_sensor_status") s.alarm = s.alarm || v.as<String>() == "alarm";
    else if (code == "smoke_sensor_state") s.alarm = s.alarm || v.as<String>() == "1";
    else if (code == "battery_percentage" && v.is<int>()) s.battery = (int8_t)constrain(v.as<int>(), 0, 100);
    else if (code == "battery_state") s.batteryLow = v.as<String>() == "low";
    else if (code == "temper_alarm") s.tamper = v.as<bool>();
    else if (code == "checking_result") s.fault = v.as<String>() == "check_failure";
  }
  if (s.battery >= 0 && s.battery <= 20) s.batteryLow = true;
  // Jednom po detektoru: svi DP-ovi sa vrednostima (nisu osetljivi), da se vidi format na pravom uređaju
  if (smokeSeen_.indexOf(s.deviceId) < 0 && !settings::isHidden(s.deviceId)) {
    smokeSeen_ += s.deviceId + ",";
    Serial.printf("[dim] %s (%s): %s\n", s.name.c_str(), s.online ? "online" : "offline", dump.c_str());
  }
  smokes_.push_back(s);
  return true;
}

void TuyaClient::printDpLog(const String& deviceId, const String& codes, uint32_t days) {
  const uint64_t now = epochMillis();
  const QueryList q = {{"type", "7"},
                       {"start_time", u64ToString(now - (uint64_t)days * 86400000ULL)},
                       {"end_time", u64ToString(now)},
                       {"size", "100"}};  // "codes" sa kodom koji uređaj nema = greška 40000303, pa filtriramo ovde
  JsonDocument doc;
  String err;
  if (!call("GET", "/v1.0/devices/" + deviceId + "/logs", q, "", doc, nullptr, err)) {
    Serial.printf("[log] %s: %s\n", deviceId.c_str(), err.c_str());
    return;
  }
  JsonArray logs = doc["result"]["logs"].as<JsonArray>();
  if (logs.size() == 0) Serial.println("[log]   (nema događaja)");
  const String want = "," + codes + ",";
  for (JsonObject l : logs) {
    if (codes.length() && want.indexOf("," + String(l["code"] | "") + ",") < 0) continue;
    const time_t t = (time_t)((l["event_time"] | 0ULL) / 1000ULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char when[24];
    strftime(when, sizeof(when), "%d.%m. %H:%M:%S", &tmv);
    Serial.printf("[log]   %s  %s = %s\n", when, String(l["code"] | "").c_str(), l["value"].as<String>().c_str());
  }
}

// Jednom u log: uređaji koje panel ne prikazuje (kategorija i nazivi DP-ova, bez vrednosti)
void TuyaClient::logUnknown(JsonObject dev, const String& category) {
  const String id = dev["id"] | "";
  if (unknownSeen_.indexOf(id) >= 0) return;
  unknownSeen_ += id + ",";
  String codes;
  for (JsonObject st : dev["status"].as<JsonArray>()) codes += String(st["code"] | "") + " ";
  Serial.printf("[tuya] nije podrzan: %s (kategorija %s) DP: %s\n", String(dev["name"] | "").c_str(),
                category.c_str(), codes.c_str());
}

bool TuyaClient::setCover(const CoverInfo& c, CoverAction a, String& err) {
  JsonDocument body;
  JsonObject cmd = body["commands"].add<JsonObject>();
  cmd["code"] = c.controlCode;
  cmd["value"] = coverActionToTuya(a);
  String json;
  serializeJson(body, json);
  JsonDocument doc;
  if (!call("POST", "/v1.0/devices/" + c.deviceId + "/commands", {}, json, doc, nullptr, err)) return false;
  if (!(doc["result"] | false)) {
    err = TR("Tuya: roletna nije prihvatila komandu", "Tuya: blind did not accept the command");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- senzori temperature

// Kodovi trenutne temperature, po prioritetu. NE "temp_value" (temperatura boje sijalice) ni "temp_set".
static const char* const TEMP_CODES[] = {"va_temperature", "temp_current", "temp_indoor", "cur_temp", "temperature"};

void TuyaClient::parseSensor(JsonObject dev) {
  JsonArray status = dev["status"].as<JsonArray>();
  String code;
  int raw = 0;
  for (const char* c : TEMP_CODES) {
    for (JsonObject st : status) {
      if (String(st["code"] | "") == c && st["value"].is<int>()) {
        code = c;
        raw = st["value"].as<int>();
        break;
      }
    }
    if (code.length()) break;
  }
  if (code.isEmpty()) return;

  SensorInfo s;
  s.deviceId = dev["id"] | "";
  s.name = dev["name"] | s.deviceId;
  s.online = dev["online"] | false;
  for (JsonObject st : status) {
    if (String(st["code"] | "") == "temp_unit_convert") s.fahrenheit = String(st["value"] | "") == "f";
  }
  float v = raw;
  for (int i = tempScale(s.deviceId, code, raw); i > 0; i--) v /= 10.0f;
  s.value = v;
  s.hasValue = true;
  sensors_.push_back(s);
}

// Broj decimala iz specifikacije uređaja ("scale"); jednom po uređaju, pa se pamti.
int TuyaClient::tempScale(const String& id, const String& code, int raw) {
  auto it = scales_.find(id);
  if (it == scales_.end() && settings::isHidden(id))  // isključen u aplikaciji: bez upita, samo procena
    return (code == "va_temperature" || abs(raw) >= 100) ? 1 : 0;
  if (it == scales_.end()) {
    int8_t sc = -1;
    JsonDocument doc;
    String err;
    if (call("GET", "/v1.0/devices/" + id + "/specifications", {}, "", doc, nullptr, err)) {
      for (JsonObject f : doc["result"]["status"].as<JsonArray>()) {
        if (String(f["code"] | "") != code) continue;
        JsonDocument v;
        if (!deserializeJson(v, (const char*)(f["values"] | "{}"))) sc = v["scale"] | 0;
      }
    } else {
      Serial.printf("[tuya] specifikacija %s: %s\n", id.c_str(), err.c_str());
    }
    it = scales_.emplace(id, sc).first;
  }
  if (it->second >= 0) return it->second;
  return (code == "va_temperature" || abs(raw) >= 100) ? 1 : 0;  // uobičajeno: desetine stepena
}

// ---------------------------------------------------------------- termostati

static float rawToC(int raw, int scale) {
  float v = raw;
  for (int i = 0; i < scale; i++) v /= 10.0f;
  return v;
}

static int32_t pow10i(int n) {
  int32_t p = 1;
  while (n-- > 0) p *= 10;
  return p;
}

// Specifikacija (opseg, korak, decimale) jednom po termostatu, pa se pamti
TuyaClient::ThermoSpec& TuyaClient::thermoSpec(const String& id, const String& curCode) {
  ThermoSpec& sp = thermoSpecs_[id];
  // Isključen u aplikaciji: bez posebnog upita (procena iz vrednosti); kad se uključi, pita se tada
  if (sp.loaded || id.startsWith("demo") || settings::isHidden(id)) return sp;
  sp.loaded = true;  // i kad ne uspe: ne ponavljaj (kvota API poziva); važe procene iz vrednosti
  JsonDocument doc;
  String err;
  if (!call("GET", "/v1.0/devices/" + id + "/specifications", {}, "", doc, nullptr, err)) {
    Serial.printf("[termostat] specifikacija %s: %s\n", id.c_str(), err.c_str());
    return sp;
  }
  auto read = [&](const char* list, const String& code, bool setRange) {
    for (JsonObject f : doc["result"][list].as<JsonArray>()) {
      if (String(f["code"] | "") != code) continue;
      JsonDocument v;
      if (deserializeJson(v, (const char*)(f["values"] | "{}"))) continue;
      if (setRange) {
        if (sp.setScale < 0) sp.setScale = v["scale"] | 0;
        if (!sp.stepRaw) {
          sp.minRaw = v["min"] | 0;
          sp.maxRaw = v["max"] | 0;
          sp.stepRaw = v["step"] | 0;
        }
      } else if (sp.curScale < 0) {
        sp.curScale = v["scale"] | 0;
      }
    }
  };
  read("functions", "temp_set", true);
  read("status", "temp_set", true);
  if (curCode.length()) read("status", curCode, false);
  Serial.printf("[termostat] specifikacija %s: temp_set %ld..%ld korak %ld, decimale %d, trenutna decimale %d\n",
                id.c_str(), (long)sp.minRaw, (long)sp.maxRaw, (long)sp.stepRaw, sp.setScale, sp.curScale);
  return sp;
}

// Termostat: kategorije "wk" (termostat) i "wkf" (glava radijatora), ili uređaj sa "temp_set" koji
// nije u listi prekidača (grejalica, klima). Utičnica sa termostatom ("wkcz") ostaje prekidač.
bool TuyaClient::parseThermostat(JsonObject dev, const String& category) {
  JsonArray status = dev["status"].as<JsonArray>();
  JsonVariant setV;
  for (JsonObject st : status)
    if (String(st["code"] | "") == "temp_set" && st["value"].is<int>()) setV = st["value"];
  const bool thermoCat = category == "wk" || category == "wkf";
  if (!thermoCat && (setV.isNull() || categoryAllowed(category))) return false;

  ThermostatInfo t;
  t.deviceId = dev["id"] | "";
  t.name = dev["name"] | t.deviceId;
  t.online = dev["online"] | false;
  String curCode;
  int curRaw = 0;
  for (const char* c : TEMP_CODES) {
    for (JsonObject st : status) {
      if (String(st["code"] | "") == c && st["value"].is<int>()) {
        curCode = c;
        curRaw = st["value"].as<int>();
        break;
      }
    }
    if (curCode.length()) break;
  }
  String dump;
  for (JsonObject st : status) {
    const String code = st["code"] | "";
    JsonVariant v = st["value"];
    dump += code + "=" + v.as<String>() + " ";
    if (code == "switch" && v.is<bool>()) {
      t.hasPower = true;
      t.on = v.as<bool>();
    } else if (code == "mode" && v.is<const char*>()) {
      t.mode = v.as<String>();
    } else if (code == "work_state" && v.is<const char*>()) {
      const String w = v.as<String>();
      t.heating = (w.indexOf("heat") >= 0 || w.indexOf("warm") >= 0 || w == "hot") ? 1 : 0;
    } else if (code == "valve_state" && v.is<const char*>()) {
      t.heating = v.as<String>() == "open" ? 1 : 0;
    } else if ((code == "valve_open_degree" || code == "valve_opening") && v.is<int>()) {
      t.heating = v.as<int>() > 0 ? 1 : 0;
    }
  }

  ThermoSpec& sp = thermoSpec(t.deviceId, curCode);
  if (curCode.length()) {
    const int sc = sp.curScale >= 0 ? sp.curScale : (curCode == "va_temperature" || abs(curRaw) > 60 ? 1 : 0);
    t.current = rawToC(curRaw, sc);
    t.hasCurrent = true;
  }
  if (!setV.isNull()) {
    const int raw = setV.as<int>();
    // Bez specifikacije: sobna temperatura je do 45°, veći broj znači desetine stepena
    const int sc = sp.setScale >= 0 ? sp.setScale : (raw > 45 ? 1 : 0);
    sp.usedSetScale = sc;
    t.target = rawToC(raw, sc);
    t.hasTarget = true;
    if (sp.stepRaw > 0 && sp.maxRaw > sp.minRaw) {
      t.minT = rawToC(sp.minRaw, sc);
      t.maxT = rawToC(sp.maxRaw, sc);
      const float st = rawToC(sp.stepRaw, sc);
      t.step = st >= 0.5f ? st : 0.5f;  // dodir menja za 0,5° (ili korak uređaja, ako je veći)
    } else {
      t.step = sc > 0 ? 0.5f : 1.0f;
    }
  }
  // Jednom po uređaju: svi DP-ovi sa vrednostima, da se vidi format na pravom uređaju
  if (newSeen_.indexOf(t.deviceId) < 0 && !settings::isHidden(t.deviceId)) {
    newSeen_ += t.deviceId + ",";
    Serial.printf("[termostat] %s (kategorija %s, %s): %s\n", t.name.c_str(), category.c_str(),
                  t.online ? "online" : "offline", dump.c_str());
  }
  thermostats_.push_back(t);
  return true;
}

bool TuyaClient::setThermostat(const ThermostatInfo& t, int8_t power, float target, String& err) {
  JsonDocument body;
  JsonArray cmds = body["commands"].to<JsonArray>();
  if (power >= 0) {
    if (!t.hasPower) {
      err = TR("termostat nema uključivanje/isključivanje", "thermostat has no on/off");
      return false;
    }
    JsonObject c = cmds.add<JsonObject>();
    c["code"] = "switch";
    c["value"] = power > 0;
  }
  if (!isnan(target)) {
    const ThermoSpec& sp = thermoSpecs_[t.deviceId];
    int32_t raw = lroundf(target * pow10i(sp.usedSetScale));
    if (sp.stepRaw > 1) raw = lroundf((float)raw / sp.stepRaw) * sp.stepRaw;
    if (sp.maxRaw > sp.minRaw) raw = constrain(raw, sp.minRaw, sp.maxRaw);
    JsonObject c = cmds.add<JsonObject>();
    c["code"] = "temp_set";
    c["value"] = raw;
  }
  if (cmds.size() == 0) return true;
  String json;
  serializeJson(body, json);
  JsonDocument doc;
  if (!call("POST", "/v1.0/devices/" + t.deviceId + "/commands", {}, json, doc, nullptr, err)) return false;
  if (!(doc["result"] | false)) {
    err = TR("Tuya: termostat nije prihvatio komandu", "Tuya: thermostat did not accept the command");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- senzori vrata i pokreta

// Vrata/prozor: "doorcontact_state" (true = otvoreno). Pokret: "pir" ("pir" = pokret) ili
// "presence_state" ("presence" = prisustvo, mmWave senzori). Uređaj iz liste prekidača (npr.
// svetlo sa senzorom pokreta) ostaje prekidač.
bool TuyaClient::parseBinary(JsonObject dev, const String& category) {
  if (categoryAllowed(category)) return false;
  BinarySensorInfo s;
  bool found = false;
  String dump;
  for (JsonObject st : dev["status"].as<JsonArray>()) {
    const String code = st["code"] | "";
    JsonVariant v = st["value"];
    dump += code + "=" + v.as<String>() + " ";
    if (code == "doorcontact_state") {
      found = true;
      s.kind = BinaryKind::Door;
      if (v.is<bool>()) s.active = v.as<bool>();
      else s.active = v.as<String>() == "true" || v.as<String>() == "open" || v.as<String>() == "1";
    } else if (code == "pir" && v.is<const char*>()) {
      found = true;
      s.kind = BinaryKind::Motion;
      s.active = v.as<String>() == "pir";
    } else if (code == "presence_state" && v.is<const char*>()) {
      found = true;
      s.kind = BinaryKind::Motion;
      s.active = v.as<String>() == "presence";
    } else if ((code == "battery_percentage" || code == "va_battery") && v.is<int>()) {
      s.battery = (int8_t)constrain(v.as<int>(), 0, 100);
    } else if (code == "battery_state") {
      s.batteryLow = v.as<String>() == "low";
    } else if (code == "temper_alarm" && v.is<bool>()) {
      s.tamper = v.as<bool>();
    }
  }
  if (!found) return false;
  if (s.battery >= 0 && s.battery <= 20) s.batteryLow = true;
  s.deviceId = dev["id"] | "";
  s.name = dev["name"] | s.deviceId;
  s.online = dev["online"] | false;
  if (newSeen_.indexOf(s.deviceId) < 0 && !settings::isHidden(s.deviceId)) {
    newSeen_ += s.deviceId + ",";
    Serial.printf("[%s] %s (kategorija %s, %s): %s\n", s.kind == BinaryKind::Door ? "vrata" : "pokret", s.name.c_str(),
                  category.c_str(), s.online ? "online" : "offline", dump.c_str());
  }
  binaries_.push_back(s);
  return true;
}

void TuyaClient::debugParse(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("[test] neispravan JSON");
    return;
  }
  std::vector<SwitchInfo> sw;
  parseDevice(doc.as<JsonObject>(), sw);
  for (const ThermostatInfo& t : thermostats_)
    Serial.printf("[test] termostat %s: %s, opseg %.1f..%.1f korak %.1f, %s, rezim '%s' (%s)\n", t.name.c_str(),
                  thermoStateText(t).c_str(), t.minT, t.maxT, t.step, t.hasPower ? "ima prekidac" : "bez prekidaca",
                  t.mode.c_str(), thermoModeText(t.mode).c_str());
  for (const BinarySensorInfo& b : binaries_)
    Serial.printf("[test] %s %s: %s, baterija %d%%%s%s\n", binaryKindName(b.kind), b.name.c_str(),
                  binaryStateText(b).c_str(), b.battery, b.batteryLow ? " slaba" : "", b.tamper ? " skinut" : "");
  for (const SensorInfo& x : sensors_) Serial.printf("[test] senzor temperature %s: %.1f\n", x.name.c_str(), x.value);
  for (const SwitchInfo& x : sw) Serial.printf("[test] prekidac %s (%s)\n", x.name.c_str(), groupName(x.group));
  thermostats_.clear();
  binaries_.clear();
  sensors_.clear();
}

bool TuyaClient::setAlarmMode(const String& deviceId, AlarmMode mode, String& err) {
  JsonDocument body;
  JsonObject cmd = body["commands"].add<JsonObject>();
  cmd["code"] = "master_mode";
  cmd["value"] = alarmModeToTuya(mode);
  String json;
  serializeJson(body, json);
  JsonDocument doc;
  if (!call("POST", "/v1.0/devices/" + deviceId + "/commands", {}, json, doc, nullptr, err)) return false;
  if (!(doc["result"] | false)) {
    err = TR("Tuya: centrala nije prihvatila komandu", "Tuya: alarm hub did not accept the command");
    return false;
  }
  return true;
}

bool TuyaClient::setSwitch(const SwitchInfo& sw, bool on, String& err) {
  JsonDocument body;
  JsonObject cmd = body["commands"].add<JsonObject>();
  cmd["code"] = sw.channel;
  cmd["value"] = on;
  String json;
  serializeJson(body, json);

  JsonDocument doc;
  if (!call("POST", "/v1.0/devices/" + sw.deviceId + "/commands", {}, json, doc, nullptr, err)) return false;
  if (!(doc["result"] | false)) {
    err = TR("Tuya: uređaj nije prihvatio komandu", "Tuya: device did not accept the command");
    return false;
  }
  return true;
}
