#include "net/cloud_task.h"

#include <WiFi.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <atomic>

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/ewelink_client.h"
#include "net/tuya_client.h"

namespace cloud {

struct Command {
  char key[96];
  bool target;
  bool alarm;      // true = key je id alarm centrale, mode je novi režim
  AlarmMode mode;
  bool cover;      // true = key je ključ roletne, action je komanda
  CoverAction action;
  bool thermo;     // true = key je termostat; power (-1 = ne menja) i temp (NAN = ne menja)
  int8_t power;
  float temp;
};

static QueueHandle_t s_queue = nullptr;
static volatile uint32_t s_lastActivity = 0;
static volatile bool s_refreshRequested = true;
static volatile bool s_demo = false;

// Wi-Fi skeniranje
static volatile bool s_scanRequested = false;
static volatile bool s_scanning = false;
static std::atomic<uint32_t> s_scanSeq{0};
static SemaphoreHandle_t s_scanMutex = nullptr;
static std::vector<WifiNetwork> s_scan;

// Cloud provajderi (menjaju se kad se promene podešavanja)
static TuyaClient* s_tuya = nullptr;
static EWeLinkClient* s_ewelink = nullptr;
static Settings s_cfg;
static uint32_t s_appliedRev = 0;

struct PollState {
  CloudProvider* provider = nullptr;
  uint32_t nextPoll = 0;
};
static PollState s_polls[2];

static constexpr uint32_t ERROR_RETRY_MS = 30000;

// ---------------------------------------------------------------- javni API (UI task)

// Uređaj isključen u aplikaciji (Uređaji): nikakva komanda, odakle god da dođe (panel, scena, telefon)
static bool disabled(const String& key) {
  if (!settings::isHidden(key)) return false;
  Serial.printf("[cloud] %s je isključen u aplikaciji, komanda nije poslata\n", key.c_str());
  return true;
}

bool requestSet(const String& key, bool target) {
  if (disabled(key)) return false;
  if (!s_queue || key.length() >= sizeof(Command::key)) return false;
  if (!g_store.beginCommand(key, target)) return false;
  Command c{};
  strncpy(c.key, key.c_str(), sizeof(c.key) - 1);
  c.target = target;
  if (xQueueSend(s_queue, &c, 0) != pdTRUE) {
    g_store.commandFinished(key, false, target);
    return false;
  }
  return true;
}

bool requestAlarm(const String& deviceId, AlarmMode mode) {
  if (disabled(deviceId)) return false;
  if (!s_queue || deviceId.length() >= sizeof(Command::key)) return false;
  if (!g_store.beginAlarmCommand(deviceId, mode)) return false;
  Command c{};
  strncpy(c.key, deviceId.c_str(), sizeof(c.key) - 1);
  c.alarm = true;
  c.mode = mode;
  if (xQueueSend(s_queue, &c, 0) != pdTRUE) {
    g_store.alarmCommandFinished(deviceId, false, mode);
    return false;
  }
  return true;
}

bool requestCover(const String& key, CoverAction a) {
  if (disabled(key)) return false;
  if (!s_queue || key.length() >= sizeof(Command::key)) return false;
  if (!g_store.beginCoverCommand(key, a)) return false;
  Command c{};
  strncpy(c.key, key.c_str(), sizeof(c.key) - 1);
  c.cover = true;
  c.action = a;
  if (xQueueSend(s_queue, &c, 0) != pdTRUE) {
    g_store.coverCommandFinished(key, false);
    return false;
  }
  return true;
}

bool requestThermostat(const String& key, int8_t power, float target) {
  if (disabled(key)) return false;
  if (!s_queue || key.length() >= sizeof(Command::key)) return false;
  if (!g_store.beginThermoCommand(key, power, target)) return false;
  Command c{};
  strncpy(c.key, key.c_str(), sizeof(c.key) - 1);
  c.thermo = true;
  c.power = power;
  c.temp = target;
  if (xQueueSend(s_queue, &c, 0) != pdTRUE) {
    g_store.thermoCommandFinished(key, false);
    return false;
  }
  return true;
}

void requestRefresh() { s_refreshRequested = true; }
static volatile bool s_smokeLogRequested = false;
static String s_logDevice;  // prazno = svi detektori dima
void requestSmokeLog(const String& deviceId) {
  s_logDevice = deviceId;
  s_smokeLogRequested = true;
}
void notifyActivity() { s_lastActivity = millis(); }
void requestWifiScan() { s_scanRequested = true; }
bool wifiScanning() { return s_scanning || s_scanRequested; }
uint32_t scanSeq() { return s_scanSeq; }
void setDemoMode(bool on) { s_demo = on; }

std::vector<WifiNetwork> scanResults() {
  xSemaphoreTake(s_scanMutex, portMAX_DELAY);
  std::vector<WifiNetwork> c = s_scan;
  xSemaphoreGive(s_scanMutex);
  return c;
}

// ---------------------------------------------------------------- interno (mrežni task)

static bool isActive() { return millis() - s_lastActivity < ACTIVE_WINDOW_MS; }
static bool timeSynced() { return time(nullptr) > 1700000000; }

static CloudProvider* providerFor(CloudKind k) {
  return k == CloudKind::Tuya ? static_cast<CloudProvider*>(s_tuya) : static_cast<CloudProvider*>(s_ewelink);
}

static void scheduleConfirm(CloudKind cloud) {
  PollState& ps = s_polls[static_cast<int>(cloud)];
  const uint32_t confirmAt = millis() + CONFIRM_AFTER_CMD_MS;
  if ((int32_t)(ps.nextPoll - confirmAt) > 0) ps.nextPoll = confirmAt;
}

static void runAlarmCommand(const Command& c) {
  const String id = c.key;
  AlarmInfo info;
  if (!g_store.findAlarm(id, info)) return;
  String err;
  const uint32_t t0 = millis();
  const bool ok = s_tuya && s_tuya->configured() && WiFi.status() == WL_CONNECTED &&
                  s_tuya->setAlarmMode(id, c.mode, err);
  if (!ok && err.isEmpty()) err = WiFi.status() == WL_CONNECTED ? "komanda nije uspela" : "nema Wi-Fi veze";
  Serial.printf("[alarm] %s -> %s: %s (%u ms) %s\n", info.name.c_str(), alarmModeToTuya(c.mode), ok ? "OK" : "GRESKA",
                (unsigned)(millis() - t0), err.c_str());
  g_store.alarmCommandFinished(id, ok, c.mode);
  if (!ok) g_store.postToast(info.name + ": " + err);
  scheduleConfirm(CloudKind::Tuya);
}

static void runCoverCommand(const Command& c) {
  const String key = c.key;
  if (s_demo && key.startsWith("demo")) {
    vTaskDelay(pdMS_TO_TICKS(400));
    g_store.coverCommandFinished(key, true);
    return;
  }
  CoverInfo info;
  if (!g_store.findCover(key, info)) return;
  String err;
  const uint32_t t0 = millis();
  const bool ok = s_tuya && s_tuya->configured() && WiFi.status() == WL_CONNECTED &&
                  s_tuya->setCover(info, c.action, err);
  if (!ok && err.isEmpty()) err = WiFi.status() == WL_CONNECTED ? "komanda nije uspela" : "nema Wi-Fi veze";
  Serial.printf("[roletna] %s -> %s: %s (%u ms) %s\n", info.name.c_str(), coverActionToTuya(c.action),
                ok ? "OK" : "GRESKA", (unsigned)(millis() - t0), err.c_str());
  g_store.coverCommandFinished(key, ok);
  if (!ok) g_store.postToast(info.name + ": " + err);
  scheduleConfirm(CloudKind::Tuya);
}

static void runThermoCommand(const Command& c) {
  const String key = c.key;
  if (s_demo && key.startsWith("demo")) {
    vTaskDelay(pdMS_TO_TICKS(400));
    g_store.thermoCommandFinished(key, true);
    return;
  }
  ThermostatInfo info;
  if (!g_store.findThermostat(key, info)) return;
  String err;
  const uint32_t t0 = millis();
  const bool ok = s_tuya && s_tuya->configured() && WiFi.status() == WL_CONNECTED &&
                  s_tuya->setThermostat(info, c.power, c.temp, err);
  if (!ok && err.isEmpty()) err = WiFi.status() == WL_CONNECTED ? "komanda nije uspela" : "nema Wi-Fi veze";
  Serial.printf("[termostat] %s ->%s%s: %s (%u ms) %s\n", info.name.c_str(),
                c.power < 0 ? "" : c.power ? " ukljuci" : " iskljuci", isnan(c.temp) ? "" : (" " + String(c.temp, 1)).c_str(),
                ok ? "OK" : "GRESKA", (unsigned)(millis() - t0), err.c_str());
  g_store.thermoCommandFinished(key, ok);
  if (!ok) g_store.postToast(info.name + ": " + err);
  scheduleConfirm(CloudKind::Tuya);
}

static void runCommand(const Command& c) {
  if (c.thermo) {
    runThermoCommand(c);
    return;
  }
  if (c.alarm) {
    runAlarmCommand(c);
    return;
  }
  if (c.cover) {
    runCoverCommand(c);
    return;
  }
  const String key = c.key;
  if (s_demo && key.startsWith("demo")) {
    vTaskDelay(pdMS_TO_TICKS(400));  // simulira kašnjenje clouda
    g_store.commandFinished(key, true, c.target);
    return;
  }
  SwitchInfo info;
  if (!g_store.findByKey(key, info)) return;
  CloudProvider* p = providerFor(info.cloud);
  String err;
  const uint32_t t0 = millis();
  const bool ok = p && p->configured() && WiFi.status() == WL_CONNECTED && p->setSwitch(info, c.target, err);
  if (!ok && err.isEmpty()) err = WiFi.status() == WL_CONNECTED ? "komanda nije uspela" : "nema Wi-Fi veze";
  Serial.printf("[cloud] %s %s -> %s (%u ms) %s\n", key.c_str(), c.target ? "ON" : "OFF", ok ? "OK" : "GRESKA",
                (unsigned)(millis() - t0), err.c_str());
  g_store.commandFinished(key, ok, c.target);
  if (!ok) g_store.postToast(info.name + ": " + err);

  scheduleConfirm(info.cloud);  // proveri stvarno stanje malo kasnije
}

// Kratak log promena alarma (bez osetljivih podataka)
static void logAlarms(const std::vector<AlarmInfo>& list) {
  static String last;
  String now;
  for (const AlarmInfo& a : list) {
    if (settings::isHidden(a.deviceId)) continue;
    now += a.name + ": " + alarmModeToTuya(a.mode) + (a.alarming ? " ALARM" : "") + (a.online ? "" : " (offline)") +
           " izlaz " + a.exitDelay + "s ulaz " + a.entryDelay + "s | ";
  }
  if (now == last) return;
  last = now;
  Serial.printf("[alarm] %s\n", now.c_str());
}

static void logSensors(const std::vector<SensorInfo>& list) {
  static String last;
  String now;
  for (const SensorInfo& s : list)
    if (!settings::isHidden(s.key())) now += s.name + " (" + s.deviceId + "): " + String(s.value, 1) + (s.fahrenheit ? "F" : "C") + (s.online ? "" : " offline") + " | ";
  if (now == last) return;
  last = now;
  Serial.printf("[senzor] %s\n", now.c_str());
}

static void logCovers(const std::vector<CoverInfo>& list) {
  static String last;
  String now;
  for (const CoverInfo& c : list)
    if (!settings::isHidden(c.key())) now += c.name + " (" + c.key() + "): " + (c.position >= 0 ? String(c.position) + "%" : String("bez polozaja")) +
           " control=" + c.lastAction + (c.online ? "" : " offline") + " | ";
  if (now == last) return;
  last = now;
  Serial.printf("[roletna] %s\n", now.c_str());
}

static void logSmokes(const std::vector<SmokeInfo>& list) {
  static String last;
  String now;
  for (const SmokeInfo& s : list)
    if (!settings::isHidden(s.key())) now += s.name + ": " + smokeStateText(s) + (s.battery >= 0 ? " bat " + String(s.battery) + "%" : "") + " | ";
  if (now == last) return;
  last = now;
  Serial.printf("[dim] %s\n", now.c_str());
}

static void logThermostats(const std::vector<ThermostatInfo>& list) {
  static String last;
  String now;
  for (const ThermostatInfo& t : list)
    if (!settings::isHidden(t.key())) now += t.name + ": " + thermoStateText(t) + (t.mode.length() ? " rezim " + t.mode : "") + " | ";
  if (now == last) return;
  last = now;
  Serial.printf("[termostat] %s\n", now.c_str());
}

static void logBinaries(const std::vector<BinarySensorInfo>& list) {
  static String last;
  String now;
  for (const BinarySensorInfo& b : list)
    if (!settings::isHidden(b.key())) now += b.name + ": " + binaryStateText(b) + (b.battery >= 0 ? " bat " + String(b.battery) + "%" : "") + " | ";
  if (now == last) return;
  last = now;
  Serial.printf("[vrata/pokret] %s\n", now.c_str());
}

static void drainCommands() {
  Command c;
  while (xQueueReceive(s_queue, &c, 0) == pdTRUE) runCommand(c);
}

static void poll(PollState& ps) {
  CloudProvider* p = ps.provider;
  std::vector<SwitchInfo> list;
  String err;
  const uint32_t t0 = millis();
  if (p->fetch(list, err)) {
    if (s_demo) return;  // demo prikaz uključen dok je čitanje bilo u toku: pravi uređaji ne ulaze
    g_store.replaceCloud(p->kind(), list);
    if (p == s_tuya) {
      g_store.replaceAlarms(s_tuya->alarms());
      logAlarms(s_tuya->alarms());
      g_store.replaceSensors(s_tuya->sensors());
      logSensors(s_tuya->sensors());
      if (!s_demo) g_store.replaceCovers(s_tuya->covers());  // demo roletna ostaje do restarta
      logCovers(s_tuya->covers());
      if (!s_demo) g_store.replaceSmokes(s_tuya->smokes());  // demo detektor ostaje do restarta
      logSmokes(s_tuya->smokes());
      if (!s_demo) g_store.replaceThermostats(s_tuya->thermostats());  // probni uređaji ostaju do restarta
      logThermostats(s_tuya->thermostats());
      if (!s_demo) g_store.replaceBinaries(s_tuya->binaries());
      logBinaries(s_tuya->binaries());
    }
    g_store.setCloudStatus(p->kind(), LinkState::Ok);
    ps.nextPoll = millis() + (isActive() ? POLL_ACTIVE_MS : POLL_IDLE_MS);
    log_i("[cloud] %s: %u prekidaca (%u ms), heap %u", cloudName(p->kind()), (unsigned)list.size(),
          (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
  } else {
    if (s_demo) return;
    Serial.printf("[cloud] %s greska: %s\n", cloudName(p->kind()), err.c_str());
    g_store.setCloudStatus(p->kind(), LinkState::Error, err);
    ps.nextPoll = millis() + ERROR_RETRY_MS;
  }
}

static void updateProviderStatus(CloudProvider* p) {
  if (p->configured()) {
    g_store.setCloudStatus(p->kind(), LinkState::Connecting);
  } else if (p->kind() == CloudKind::EWeLink && s_cfg.ewAppId.length()) {
    g_store.setCloudStatus(p->kind(), LinkState::Disabled, "nalog nije povezan");
  } else {
    g_store.setCloudStatus(p->kind(), LinkState::Disabled, "nije podešen");
  }
}

// Primeni nova podešavanja bez restarta
static void applySettings() {
  const uint32_t rev = settings::revision();
  if (rev == s_appliedRev) return;
  const bool first = s_appliedRev == 0;
  s_appliedRev = rev;
  const Settings n = settings::get();

  if (first || n.wifiSsid != s_cfg.wifiSsid || n.wifiPass != s_cfg.wifiPass) {
    if (n.wifiSsid.length()) Serial.printf("[cloud] Wi-Fi: povezivanje na '%s'\n", n.wifiSsid.c_str());
    else Serial.println("[cloud] Wi-Fi: mreža nije podešena");
    WiFi.disconnect();
    if (n.wifiSsid.length()) WiFi.begin(n.wifiSsid.c_str(), n.wifiPass.c_str());
  }

  if (first || n.tuyaEndpoint != s_cfg.tuyaEndpoint || n.tuyaId != s_cfg.tuyaId ||
      n.tuyaSecret != s_cfg.tuyaSecret) {
    delete s_tuya;
    s_tuya = new TuyaClient(n.tuyaEndpoint.c_str(), n.tuyaId.c_str(), n.tuyaSecret.c_str(), TUYA_CATEGORIES);
    std::vector<SwitchInfo> none;
    if (!first) {
      g_store.replaceCloud(CloudKind::Tuya, none);
      g_store.replaceAlarms({});
      g_store.replaceSensors({});
      g_store.replaceCovers({});
      g_store.replaceSmokes({});
      g_store.replaceThermostats({});
      g_store.replaceBinaries({});
    }
  }

  // eWeLink: uvek ponovo učitaj (tokeni su možda promenjeni prijavom/odjavom)
  const bool ewCredsChanged = n.ewAppId != s_cfg.ewAppId || n.ewAppSecret != s_cfg.ewAppSecret;
  delete s_ewelink;
  s_ewelink = new EWeLinkClient(n.ewAppId.c_str(), n.ewAppSecret.c_str());
  s_ewelink->loadTokens();
  if (!first && (ewCredsChanged || !s_ewelink->hasTokens())) {
    std::vector<SwitchInfo> none;
    g_store.replaceCloud(CloudKind::EWeLink, none);
  }

  s_cfg = n;
  s_polls[0].provider = s_tuya;
  s_polls[1].provider = s_ewelink;
  for (auto& ps : s_polls) {
    updateProviderStatus(ps.provider);
    ps.nextPoll = millis();
  }
}

static void doScan() {
  s_scanning = true;
  s_scanRequested = false;
  const bool wasConnected = WiFi.status() == WL_CONNECTED;
  if (!wasConnected) WiFi.disconnect();  // prekini pokušaje povezivanja da skeniranje ne bi palo
  const int n = WiFi.scanNetworks(false, false);
  std::vector<WifiNetwork> found;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    auto it = std::find_if(found.begin(), found.end(), [&](const WifiNetwork& w) { return w.ssid == ssid; });
    if (it != found.end()) {
      if (WiFi.RSSI(i) > it->rssi) it->rssi = WiFi.RSSI(i);
      continue;
    }
    found.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(found.begin(), found.end(), [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
  if (!wasConnected && s_cfg.wifiSsid.length()) WiFi.begin(s_cfg.wifiSsid.c_str(), s_cfg.wifiPass.c_str());

  xSemaphoreTake(s_scanMutex, portMAX_DELAY);
  s_scan.swap(found);
  xSemaphoreGive(s_scanMutex);
  Serial.printf("[cloud] Wi-Fi skeniranje: %d mreza\n", n);
  s_scanSeq++;
  s_scanning = false;
}

static void task(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("smart-light-panel");
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // brži odziv komandi
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);

  bool timeLogged = false;
  bool wasActive = false;
  for (;;) {
    applySettings();
    if (s_scanRequested) doScan();

    const bool up = WiFi.status() == WL_CONNECTED;
    g_store.setWifi(up, up ? WiFi.localIP().toString() : String());
    if (!timeLogged && timeSynced()) {
      timeLogged = true;
      g_store.setTimeSynced(true);
      Serial.println("[cloud] vreme sinhronizovano (NTP)");
    }

    if (!up || !timeSynced()) {
      drainCommands();  // demo komande i jasna greška umesto večitog čekanja
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    drainCommands();

    if (s_smokeLogRequested && s_tuya && s_tuya->configured()) {
      s_smokeLogRequested = false;
      if (s_logDevice.length()) {
        Serial.printf("[log] %s, poslednjih 7 dana:\n", s_logDevice.c_str());
        if (settings::isHidden(s_logDevice)) Serial.println("[log] uređaj je isključen u aplikaciji");
        else s_tuya->printDpLog(s_logDevice, "", 7);
      } else
      for (const SmokeInfo& s : s_tuya->smokes()) {
        if (settings::isHidden(s.key())) continue;  // isključen u aplikaciji: bez upita
        Serial.printf("[log] %s (%s), poslednjih 7 dana:\n", s.name.c_str(), s.deviceId.c_str());
        s_tuya->printDpLog(s.deviceId, "smoke_sensor_status,link_alarm,checking_result,temper_alarm", 7);
      }
    }

    // Prelazak u aktivni režim = osveži odmah
    const bool active = isActive();
    if (active && !wasActive) s_refreshRequested = true;
    wasActive = active;

    const bool force = s_refreshRequested;
    s_refreshRequested = false;
    for (auto& ps : s_polls) {
      if (s_demo) break;  // demo prikaz: ništa se ne čita sa naloga (pravi uređaji ne smeju na snimke)
      if (!ps.provider || !ps.provider->configured()) continue;
      if (force || (int32_t)(millis() - ps.nextPoll) >= 0) {
        poll(ps);
        drainCommands();  // komanda koja je stigla u međuvremenu ne čeka drugi cloud
      }
      if (s_ewelink && ps.provider == s_ewelink && s_ewelink->sessionExpired()) {
        g_store.setCloudStatus(CloudKind::EWeLink, LinkState::Error,
                               "eWeLink: prijava istekla - ponovo poveži nalog (Podešavanja)");
      }
    }

    Command c;
    xQueuePeek(s_queue, &c, pdMS_TO_TICKS(200));  // čekaj komandu ili sledeći krug
  }
}

void begin() {
  s_queue = xQueueCreate(40, sizeof(Command));  // scena može poslati više komandi odjednom
  s_scanMutex = xSemaphoreCreateMutex();
  s_lastActivity = millis();
  xTaskCreatePinnedToCore(task, "cloud", 20 * 1024, nullptr, 3, nullptr, 0);
}

}  // namespace cloud
