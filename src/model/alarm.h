#pragma once
#include <Arduino.h>

// Tuya alarm centrala (kategorija "mal"): DP master_mode / master_state / delay_set / alarm_delay_time.
enum class AlarmMode : uint8_t { Unknown, Disarmed, Away, Home, Sos };

inline AlarmMode alarmModeFromTuya(const String& v) {
  if (v == "disarmed") return AlarmMode::Disarmed;
  if (v == "arm") return AlarmMode::Away;
  if (v == "home") return AlarmMode::Home;
  if (v == "sos") return AlarmMode::Sos;
  return AlarmMode::Unknown;
}

inline const char* alarmModeToTuya(AlarmMode m) {
  switch (m) {
    case AlarmMode::Disarmed: return "disarmed";
    case AlarmMode::Away: return "arm";
    case AlarmMode::Home: return "home";
    case AlarmMode::Sos: return "sos";
    default: return "";
  }
}

inline bool alarmArmed(AlarmMode m) { return m == AlarmMode::Away || m == AlarmMode::Home; }

struct AlarmInfo {
  String deviceId;
  String name;
  bool online = false;
  AlarmMode mode = AlarmMode::Unknown;
  bool alarming = false;    // master_state == "alarm"
  uint16_t exitDelay = 0;   // delay_set, sekunde
  uint16_t entryDelay = 0;  // alarm_delay_time, sekunde
  String lastMsg;           // dekodiran alarm_msg (npr. "System Alarm: Door/Window (03 Zone)(Otvor - sredina)")

  // Ključ za sakrivanje (isti prostor ključeva kao prekidači)
  String key() const { return deviceId; }
};

struct AlarmState : AlarmInfo {
  bool pending = false;     // komanda poslata, čeka se odgovor
  uint32_t holdUntil = 0;   // do tada ignorišemo stara stanja iz osvežavanja
  uint32_t exitUntil = 0;   // millis() kraja izlaznog kašnjenja, 0 = nema odbrojavanja
};
