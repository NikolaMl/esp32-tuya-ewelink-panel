#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <vector>

#include "model/alarm.h"
#include "model/cover.h"
#include "model/smoke.h"
#include "model/sensor.h"
#include "model/switch_info.h"
#include "model/binary_sensor.h"
#include "model/thermostat.h"

enum class LinkState : uint8_t { Disabled, Connecting, Ok, Error };

struct CloudStatus {
  LinkState state = LinkState::Disabled;
  String message;
};

struct SwitchState : SwitchInfo {
  bool pending = false;         // komanda poslata, čeka se odgovor
  uint32_t holdUntil = 0;       // do tada ignorišemo "stara" stanja iz osvežavanja
};

struct SystemStatus {
  bool wifiConnected = false;
  bool timeSynced = false;
  String ip;
  CloudStatus tuya;
  CloudStatus ewelink;
  bool initialLoadDone = false;
  String toast;            // poslednja poruka o grešci za kratak prikaz
  uint32_t toastSeq = 0;   // raste sa svakom novom porukom
};

// Deljeno stanje između mrežnog taska (piše) i UI taska (čita).
// Svaka izmena podiže revision(); promena skupa prekidača podiže i layoutRevision().
class DeviceStore {
 public:
  void begin();

  // ---- mrežni task
  void replaceCloud(CloudKind cloud, std::vector<SwitchInfo>& fresh);
  void commandFinished(const String& key, bool ok, bool target);
  bool findByKey(const String& key, SwitchInfo& out);
  void setWifi(bool connected, const String& ip);
  void setTimeSynced(bool synced);
  void setCloudStatus(CloudKind cloud, LinkState st, const String& msg = "");
  void postToast(const String& msg);
  void replaceAlarms(const std::vector<AlarmInfo>& fresh);
  void replaceSensors(std::vector<SensorInfo> fresh);
  void alarmCommandFinished(const String& deviceId, bool ok, AlarmMode target);
  bool findAlarm(const String& deviceId, AlarmInfo& out);
  void replaceCovers(const std::vector<CoverInfo>& fresh);
  void replaceSmokes(std::vector<SmokeInfo> fresh);
  void coverCommandFinished(const String& key, bool ok);
  bool findCover(const String& key, CoverInfo& out);
  void replaceThermostats(const std::vector<ThermostatInfo>& fresh);
  void thermoCommandFinished(const String& key, bool ok);
  bool findThermostat(const String& key, ThermostatInfo& out);
  void replaceBinaries(const std::vector<BinarySensorInfo>& fresh);

  // ---- UI task
  // Vraća false ako je prekidač nepoznat, offline ili već čeka odgovor.
  bool beginCommand(const String& key, bool target);
  std::vector<SwitchState> snapshot();
  // Alarm: false ako je centrala nepoznata, offline ili već čeka odgovor.
  // Za uključivanje odmah počinje odbrojavanje izlaznog kašnjenja.
  bool beginAlarmCommand(const String& deviceId, AlarmMode target);
  std::vector<AlarmState> alarms();
  std::vector<SensorInfo> sensors();
  // Roletna: false ako je nepoznata, offline ili već čeka odgovor
  bool beginCoverCommand(const String& key, CoverAction a);
  std::vector<CoverState> covers();
  std::vector<SmokeInfo> smokes();
  // Termostat: false ako je nepoznat ili offline. Prikaz odmah prelazi na novu vrednost.
  // power: -1 = ne menja; target: NAN = ne menja
  bool beginThermoCommand(const String& key, int8_t power, float target);
  std::vector<ThermostatState> thermostats();
  std::vector<BinarySensorState> binaries();
  SystemStatus systemStatus();
  uint32_t revision() const { return revision_; }
  uint32_t layoutRevision() const { return layoutRevision_; }

 private:
  void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex_); }
  void sortLocked();

  SemaphoreHandle_t mutex_ = nullptr;
  std::vector<SwitchState> items_;
  std::vector<AlarmState> alarms_;
  std::vector<SensorInfo> sensors_;
  std::vector<CoverState> covers_;
  std::vector<SmokeInfo> smokes_;
  std::vector<ThermostatState> thermostats_;
  std::vector<BinarySensorState> binaries_;
  SystemStatus sys_;
  std::atomic<uint32_t> revision_{1};
  std::atomic<uint32_t> layoutRevision_{1};
};

extern DeviceStore g_store;
