#pragma once
#include <ArduinoJson.h>

#include "model/alarm.h"
#include "model/binary_sensor.h"
#include "model/cover.h"
#include "model/thermostat.h"
#include "model/smoke.h"
#include "model/sensor.h"

#include <map>
#include "net/cloud_provider.h"
#include "net/https_session.h"

using QueryList = std::vector<std::pair<String, String>>;

// Tuya Cloud OpenAPI (iot.tuya.com projekat povezan sa Smart Life nalogom).
// Dokumentacija potpisa: https://developer.tuya.com/en/docs/iot/new-singnature
class TuyaClient : public CloudProvider {
 public:
  TuyaClient(const char* endpoint, const char* accessId, const char* accessSecret, const char* categories);

  CloudKind kind() const override { return CloudKind::Tuya; }
  bool configured() const override;
  bool fetch(std::vector<SwitchInfo>& out, String& err) override;
  bool setSwitch(const SwitchInfo& sw, bool on, String& err) override;

  // Alarm centrale (kategorija "mal") pronađene pri poslednjem fetch()
  const std::vector<AlarmInfo>& alarms() const { return alarms_; }
  bool setAlarmMode(const String& deviceId, AlarmMode mode, String& err);

  // Senzori temperature pronađeni pri poslednjem fetch()
  const std::vector<SensorInfo>& sensors() const { return sensors_; }

  // Roletne (moduli za roletne) pronađene pri poslednjem fetch()
  const std::vector<CoverInfo>& covers() const { return covers_; }
  bool setCover(const CoverInfo& c, CoverAction a, String& err);

  // Detektori dima pronađeni pri poslednjem fetch()
  const std::vector<SmokeInfo>& smokes() const { return smokes_; }
  // Termostati (wk, wkf i uređaji sa "temp_set") pronađeni pri poslednjem fetch()
  const std::vector<ThermostatInfo>& thermostats() const { return thermostats_; }
  // power: -1 = ne menja, 0/1 = isključi/uključi; target: NAN = ne menja
  bool setThermostat(const ThermostatInfo& t, int8_t power, float target, String& err);

  // Senzori vrata/prozora i pokreta pronađeni pri poslednjem fetch()
  const std::vector<BinarySensorInfo>& binaries() const { return binaries_; }

  // Debug: obradi jedan uređaj iz JSON-a (kao u odgovoru Tuya) i ispiši šta je panel prepoznao.
  // Uređaji sa id "demo..." ne traže specifikaciju sa Tuya.
  void debugParse(const char* json);

  // Debug: istorija DP-ova uređaja (Tuya logs, type=7) u Serial, najnovije prvo
  void printDpLog(const String& deviceId, const String& codes, uint32_t days);

  // Javno zbog testiranja na računaru (test/host_sign_test.cpp)
  static String buildStringToSign(const String& method, const String& body, const String& pathWithQuery);
  static String buildSign(const String& accessId, const String& secret, const String& token, const String& t,
                          const String& stringToSign);
  static String sortedQuery(QueryList q, bool encode);

 private:
  bool ensureToken(String& err);
  bool call(const char* method, const String& path, const QueryList& query, const String& body,
            JsonDocument& doc, const JsonDocument* filter, String& err, bool allowRetry = true);
  bool categoryAllowed(const String& cat) const;
  static String describeError(int code, const String& msg);
  void parseAlarm(JsonObject dev);
  void parseSensor(JsonObject dev);
  bool parseCover(JsonObject dev, const String& category);
  bool parseSmoke(JsonObject dev, const String& category);
  bool parseThermostat(JsonObject dev, const String& category);
  bool parseBinary(JsonObject dev, const String& category);
  void parseDevice(JsonObject dev, std::vector<SwitchInfo>& out);
  struct ThermoSpec {
    bool loaded = false;      // specifikacija pročitana sa Tuya
    int8_t setScale = -1, curScale = -1;  // broj decimala temp_set / trenutne temperature (-1 = nepoznato)
    int32_t minRaw = 0, maxRaw = 0, stepRaw = 0;  // temp_set u jedinicama uređaja (0 = nepoznato)
    int8_t usedSetScale = 1;  // koji je broj decimala korišćen pri poslednjem čitanju (za komandu)
  };
  ThermoSpec& thermoSpec(const String& id, const String& curCode);
  void logUnknown(JsonObject dev, const String& category);
  int tempScale(const String& id, const String& code, int raw);

  String endpoint_, accessId_, secret_, categories_;
  String token_;
  uint32_t tokenValidUntil_ = 0;  // millis()
  std::vector<AlarmInfo> alarms_;
  std::vector<SensorInfo> sensors_;
  std::vector<CoverInfo> covers_;
  std::vector<SmokeInfo> smokes_;
  std::vector<ThermostatInfo> thermostats_;
  std::vector<BinarySensorInfo> binaries_;
  std::map<String, ThermoSpec> thermoSpecs_;
  String newSeen_;      // termostati i senzori vrata/pokreta čiji su DP-ovi već ispisani u log
  String smokeSeen_;    // detektori čiji su DP-ovi već ispisani u log
  String unknownSeen_;  // uređaji koje panel ne prepoznaje (za log, jednom)
  std::map<String, int8_t> scales_;  // id -> broj decimala temperature (iz specifikacije, -1 = nepoznato)
  HttpsSession http_{"tuya"};
};
