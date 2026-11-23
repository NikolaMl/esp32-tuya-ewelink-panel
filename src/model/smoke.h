#pragma once
#include <Arduino.h>

// Detektor dima (Tuya kategorija "ywbj"). Stanje se čita pri istom osvežavanju kao ostali uređaji
// (10 s dok se panel koristi, 5 min dok miruje), pa panel može da kasni; detektor sam pišti.
struct SmokeInfo {
  String deviceId;
  String name;
  bool online = false;
  bool alarm = false;       // smoke_sensor_status == "alarm" ili smoke_sensor_state == "1"
  int8_t battery = -1;      // battery_percentage, -1 = nepoznato
  bool batteryLow = false;  // battery_state == "low" ili procenat <= 20
  bool tamper = false;      // temper_alarm (skinut sa nosača)
  bool fault = false;       // checking_result == "check_failure" (samotest nije prošao)

  // Ključ za raspored i sakrivanje (isti prostor ključeva kao ostali uređaji)
  String key() const { return deviceId; }
};

// Kratak opis stanja za listu i telefon
inline String smokeStateText(const SmokeInfo& s) {
  if (!s.online) return "Nedostupan";
  if (s.alarm) return "DIM!";
  if (s.fault) return "Samotest nije prošao";
  if (s.tamper) return "Skinut sa nosača";
  if (s.batteryLow) return s.battery >= 0 ? "Slaba baterija " + String(s.battery) + "%" : String("Slaba baterija");
  return "U redu";
}
