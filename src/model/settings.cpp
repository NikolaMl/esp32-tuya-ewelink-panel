#include "model/settings.h"

#include <Preferences.h>

#include <algorithm>
#include <atomic>
#include <vector>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "util/crypto.h"

// Opcione početne vrednosti iz include/secrets.h
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef TUYA_ACCESS_ID
#define TUYA_ACCESS_ID ""
#endif
#ifndef TUYA_ACCESS_SECRET
#define TUYA_ACCESS_SECRET ""
#endif
#ifndef EWELINK_APP_ID
#define EWELINK_APP_ID ""
#endif
#ifndef EWELINK_APP_SECRET
#define EWELINK_APP_SECRET ""
#endif

const TuyaRegion TUYA_REGIONS[] = {
    {"Central Europe", "https://openapi.tuyaeu.com"},
    {"Western Europe", "https://openapi-weaz.tuyaeu.com"},
    {"Western America", "https://openapi.tuyaus.com"},
    {"Eastern America", "https://openapi-ueaz.tuyaus.com"},
    {"China", "https://openapi.tuyacn.com"},
    {"India", "https://openapi.tuyain.com"},
};
const size_t TUYA_REGION_COUNT = sizeof(TUYA_REGIONS) / sizeof(TUYA_REGIONS[0]);

namespace settings {

static constexpr const char* NS = "settings";
static SemaphoreHandle_t s_mutex;
static Settings s_cur;
static std::atomic<uint32_t> s_rev{1};
static String s_pin;
static std::vector<String> s_hidden;
static String s_pinHash, s_pinSalt;
static std::vector<Position> s_pos;
static std::vector<String> s_pages{String()};  // nazivi delova početnog ekrana; broj delova = size()
static ScreenCfg s_screen;
static std::atomic<uint32_t> s_uiRev{1};
static void seedDefaultsLocked();
static std::atomic<uint32_t> s_hiddenRev{1};

static String readKey(Preferences& p, const char* key, const char* def) {
  return p.isKey(key) ? p.getString(key, def) : String(def);
}

void begin() {
  s_mutex = xSemaphoreCreateMutex();
  Preferences p;
  p.begin(NS, true);
  s_cur.wifiSsid = readKey(p, "wifiSsid", WIFI_SSID);
  s_cur.wifiPass = readKey(p, "wifiPass", WIFI_PASSWORD);
  s_cur.tuyaEndpoint = readKey(p, "tuyaEp", TUYA_ENDPOINT);
  s_cur.tuyaId = readKey(p, "tuyaId", TUYA_ACCESS_ID);
  s_cur.tuyaSecret = readKey(p, "tuyaSecret", TUYA_ACCESS_SECRET);
  s_cur.ewAppId = readKey(p, "ewAppId", EWELINK_APP_ID);
  s_cur.ewAppSecret = readKey(p, "ewSecret", EWELINK_APP_SECRET);
  // Sakriveni prekidači: ključevi razdvojeni novim redom
  const String hidden = readKey(p, "hidden", "");
  // Raspored početnog ekrana; prvi red "#" = korisnik ga je sačuvao (može biti i prazan)
  const String layout = readKey(p, "layout2", "");
  for (int i = 0; i < (int)layout.length();) {
    int nl = layout.indexOf('\n', i);
    if (nl < 0) nl = layout.length();
    const String line = layout.substring(i, nl);
    // "ključ x y [izgled [deo]]" (stariji zapisi nemaju izgled ni deo)
    const int a = line.indexOf(' '), b = a > 0 ? line.indexOf(' ', a + 1) : -1;
    const int c = b > 0 ? line.indexOf(' ', b + 1) : -1;
    const int d = c > 0 ? line.indexOf(' ', c + 1) : -1;
    if (a > 0 && b > a) {
      Position pos{line.substring(0, a), (int16_t)line.substring(a + 1, b).toInt(),
                   (int16_t)line.substring(b + 1, c > 0 ? c : line.length()).toInt()};
      if (c > 0)
        pos.style = (uint8_t)constrain(line.substring(c + 1, d > 0 ? d : line.length()).toInt(), 0, (long)STYLE_INVISIBLE);
      if (d > 0) pos.page = (uint8_t)constrain(line.substring(d + 1).toInt(), 0, HOME_MAX_PAGES - 1);
      if (s_pos.size() < MAX_POSITIONS) s_pos.push_back(pos);
    }
    i = nl + 1;
  }
  s_screen.dimSec = p.isKey("dimSec") ? p.getUShort("dimSec") : SCREEN_DIM_AFTER_MS / 1000;
  s_screen.offSec = p.isKey("offSec") ? p.getUShort("offSec") : SCREEN_OFF_AFTER_MS / 1000;
  s_pinHash = readKey(p, "alarmPinH", "");
  s_pinSalt = readKey(p, "alarmPinS", "");
  const String pages = readKey(p, "pageNames", "");
  p.end();
  int start = 0;
  while (start < (int)hidden.length()) {
    int nl = hidden.indexOf('\n', start);
    if (nl < 0) nl = hidden.length();
    if (nl > start) s_hidden.push_back(hidden.substring(start, nl));
    start = nl + 1;
  }

  if (!layout.length()) seedDefaultsLocked();  // raspored nikad nije sačuvan

  // Delovi početnog ekrana: nazivi razdvojeni novim redom (prazan naziv je dozvoljen)
  s_pages.clear();
  for (int i = 0;;) {
    const int nl = pages.indexOf('\n', i);
    s_pages.push_back(pages.substring(i, nl < 0 ? pages.length() : nl));
    if (nl < 0 || s_pages.size() >= HOME_MAX_PAGES) break;
    i = nl + 1;
  }
  for (Position& q : s_pos)
    if (q.page >= s_pages.size()) q.page = 0;

  char buf[8];
  snprintf(buf, sizeof(buf), "%06u", (unsigned)(esp_random() % 1000000));
  s_pin = buf;

  Serial.printf("[settings] Wi-Fi: %s, Tuya: %s, eWeLink: %s\n",
                s_cur.wifiSsid.length() ? s_cur.wifiSsid.c_str() : "(nije podešen)",
                s_cur.tuyaId.length() ? "podešen" : "nije podešen",
                s_cur.ewAppId.length() ? "podešen" : "nije podešen");
}

Settings get() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  Settings c = s_cur;
  xSemaphoreGive(s_mutex);
  return c;
}

static void store(const char* key, const String& v) {
  Preferences p;
  p.begin(NS, false);
  p.putString(key, v);
  p.end();
}

void setWifi(const String& ssid, const String& pass) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cur.wifiSsid = ssid;
  s_cur.wifiPass = pass;
  xSemaphoreGive(s_mutex);
  store("wifiSsid", ssid);
  store("wifiPass", pass);
  s_rev++;
}

void setTuya(const String& endpoint, const String& id, const String& secret) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cur.tuyaEndpoint = endpoint;
  s_cur.tuyaId = id;
  s_cur.tuyaSecret = secret;
  xSemaphoreGive(s_mutex);
  store("tuyaEp", endpoint);
  store("tuyaId", id);
  store("tuyaSecret", secret);
  s_rev++;
}

void setEWeLink(const String& appId, const String& appSecret) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_cur.ewAppId = appId;
  s_cur.ewAppSecret = appSecret;
  xSemaphoreGive(s_mutex);
  store("ewAppId", appId);
  store("ewSecret", appSecret);
  s_rev++;
}

void touchEWeLinkTokens() { s_rev++; }

bool isHidden(const String& key) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const bool h = std::find(s_hidden.begin(), s_hidden.end(), key) != s_hidden.end();
  xSemaphoreGive(s_mutex);
  return h;
}

void setHidden(const String& key, bool hidden) {
  String joined;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  auto it = std::find(s_hidden.begin(), s_hidden.end(), key);
  const bool changed = hidden ? it == s_hidden.end() : it != s_hidden.end();
  if (changed) {
    if (hidden) s_hidden.push_back(key);
    else s_hidden.erase(it);
    for (const String& k : s_hidden) joined += k + "\n";
  }
  xSemaphoreGive(s_mutex);
  if (!changed) return;
  store("hidden", joined);
  s_hiddenRev++;
}

uint32_t hiddenRevision() { return s_hiddenRev; }

bool movePosition(size_t index, const String& key, int16_t x, int16_t y) {
  bool ok = false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (index < s_pos.size() && s_pos[index].key == key) {
    s_pos[index].x = x;
    s_pos[index].y = y;
    ok = true;
  }
  xSemaphoreGive(s_mutex);
  return ok;
}

void savePositions() {
  String out = "#\n";
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (const Position& p : s_pos) {
    out += p.key + " " + p.x + " " + p.y;
    if (p.style != STYLE_BUTTON || p.page) out += " " + String((int)p.style);
    if (p.page) out += " " + String((int)p.page);
    out += "\n";
  }
  xSemaphoreGive(s_mutex);
  store("layout2", out);
}

std::vector<Position> positions() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  std::vector<Position> c = s_pos;
  xSemaphoreGive(s_mutex);
  return c;
}

void replacePositions(const std::vector<Position>& list) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_pos.assign(list.begin(), list.begin() + std::min(list.size(), MAX_POSITIONS));
  xSemaphoreGive(s_mutex);
  savePositions();
  s_uiRev++;
}

// Podrazumevani raspored iz config.h, bez uređaja sakrivenih u Podešavanja → Uređaji
static void seedDefaultsLocked() {
  s_pos.clear();
  for (const HomePos* h = HOME_POSITIONS; h->key; h++) {
    if (std::find(s_hidden.begin(), s_hidden.end(), String(h->key)) == s_hidden.end())
      s_pos.push_back(Position{h->key, h->x, h->y});
  }
}

// ---------------------------------------------------------------- delovi početnog ekrana

static void savePagesLocked() {
  String out;
  for (size_t i = 0; i < s_pages.size(); i++) {
    if (i) out += '\n';
    out += s_pages[i];
  }
  store("pageNames", out);
}

static String cleanName(String n) {
  n.replace("\n", " ");
  n.trim();
  return n.substring(0, 32);
}

std::vector<String> pageNames() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  std::vector<String> c = s_pages;
  xSemaphoreGive(s_mutex);
  return c;
}

uint8_t pageCount() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const uint8_t n = s_pages.size();
  xSemaphoreGive(s_mutex);
  return n;
}

bool addPage(const String& name) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const bool ok = s_pages.size() < HOME_MAX_PAGES;
  if (ok) {
    s_pages.push_back(cleanName(name));
    savePagesLocked();
  }
  xSemaphoreGive(s_mutex);
  if (ok) s_uiRev++;
  return ok;
}

void renamePages(const std::vector<String>& names) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (size_t i = 0; i < s_pages.size() && i < names.size(); i++) s_pages[i] = cleanName(names[i]);
  savePagesLocked();
  xSemaphoreGive(s_mutex);
  s_uiRev++;
}

void deletePage(uint8_t page) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_pages.size() > 1 && page < s_pages.size()) {
    s_pages.erase(s_pages.begin() + page);
    savePagesLocked();
    std::vector<Position> keep;
    for (Position p : s_pos) {
      if (p.page == page) continue;
      if (p.page > page) p.page--;
      keep.push_back(p);
    }
    s_pos.swap(keep);
  }
  xSemaphoreGive(s_mutex);
  savePositions();
  s_uiRev++;
}

void resetPositions() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  seedDefaultsLocked();
  xSemaphoreGive(s_mutex);
  savePositions();
  s_uiRev++;
}

ScreenCfg screen() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  ScreenCfg c = s_screen;
  xSemaphoreGive(s_mutex);
  return c;
}

void setScreen(const ScreenCfg& c) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_screen = c;
  xSemaphoreGive(s_mutex);
  Preferences p;
  p.begin(NS, false);
  p.putUShort("dimSec", c.dimSec);
  p.putUShort("offSec", c.offSec);
  p.end();
  s_uiRev++;
}

uint32_t uiRevision() { return s_uiRev; }
void touchUi() { s_uiRev++; }

bool hasAlarmPin() { return s_pinHash.length() > 0; }

static uint8_t s_pinFails = 0;
static uint32_t s_pinLockUntil = 0;

uint32_t alarmPinLockedMs() {
  if (!s_pinLockUntil) return 0;
  const int32_t left = (int32_t)(s_pinLockUntil - millis());
  return left > 0 ? left : 0;
}

static bool checkAlarmPin(const String& pin) {
  if (!hasAlarmPin()) return false;
  const String h = crypto::sha256Hex(s_pinSalt + pin);
  // poređenje bez ranog izlaska
  uint8_t diff = h.length() != s_pinHash.length();
  for (size_t i = 0; i < h.length() && i < s_pinHash.length(); i++) diff |= h[i] ^ s_pinHash[i];
  return diff == 0;
}

PinResult verifyAlarmPin(const String& pin) {
  if (alarmPinLockedMs()) return PinResult::Locked;
  if (checkAlarmPin(pin)) {
    s_pinFails = 0;
    s_pinLockUntil = 0;
    return PinResult::Ok;
  }
  s_pinFails++;
  if (s_pinFails >= 5) {
    const uint8_t extra = s_pinFails - 5;
    s_pinLockUntil = (millis() + 30000UL * (1UL << (extra < 5 ? extra : 5))) | 1;  // 30 s … 16 min
  }
  Serial.printf("[alarm] pogrešan PIN (%u. put)\n", (unsigned)s_pinFails);
  return PinResult::Wrong;
}

void setAlarmPin(const String& pin) {
  s_pinSalt = crypto::randomAlnum(16);
  s_pinHash = crypto::sha256Hex(s_pinSalt + pin);
  store("alarmPinS", s_pinSalt);
  store("alarmPinH", s_pinHash);
}

uint32_t revision() { return s_rev; }

const String& webPin() { return s_pin; }

}  // namespace settings
