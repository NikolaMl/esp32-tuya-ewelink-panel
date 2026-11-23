#pragma once
#include <Arduino.h>
#include <time.h>

// Senzor vrata/prozora (Tuya "mcs": doorcontact_state) i senzor pokreta/prisustva ("pir": pir,
// "hps": presence_state). Stanje se čita pri istom osvežavanju kao ostali uređaji (10 s dok se
// panel koristi, 5 min dok miruje), pa kratak pokret između dva osvežavanja panel ne vidi.
enum class BinaryKind : uint8_t { Door, Motion };

struct BinarySensorInfo {
  String deviceId;
  String name;
  BinaryKind kind = BinaryKind::Door;
  bool online = false;
  bool active = false;      // vrata otvorena / pokret (prisustvo)
  int8_t battery = -1;      // procenat, -1 = nepoznato
  bool batteryLow = false;  // battery_state == "low" ili procenat <= 20
  bool tamper = false;      // temper_alarm (skinut / otvoren)

  // Ključ za raspored i sakrivanje (isti prostor ključeva kao ostali uređaji)
  String key() const { return deviceId; }
};

struct BinarySensorState : BinarySensorInfo {
  time_t changedAt = 0;   // kad je panel video promenu stanja (0 = od pokretanja nije bilo promene)
  time_t lastActive = 0;  // poslednji put viđeno "otvoreno" / "pokret"
};

inline const char* binaryKindName(BinaryKind k) { return k == BinaryKind::Door ? "Senzor vrata" : "Senzor pokreta"; }

// "Otvoreno" / "Zatvoreno" / "Pokret" / "Mirno" / "Nedostupan"
inline String binaryStateText(const BinarySensorInfo& s) {
  if (!s.online) return "Nedostupan";
  if (s.kind == BinaryKind::Door) return s.active ? "Otvoreno" : "Zatvoreno";
  return s.active ? "Pokret" : "Mirno";
}

// Dodatak uz stanje: "od 14:32" (vrata), "poslednji u 14:32" (pokret), upozorenja
inline String binaryNote(const BinarySensorState& s) {
  String n;
  auto hm = [](time_t t) {
    struct tm v;
    localtime_r(&t, &v);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", v.tm_hour, v.tm_min);
    return String(b);
  };
  if (s.online && s.kind == BinaryKind::Door && s.changedAt) n = "od " + hm(s.changedAt);
  if (s.online && s.kind == BinaryKind::Motion && s.lastActive) n = (s.active ? "u " : "poslednji u ") + hm(s.lastActive);
  if (s.online && s.tamper) n += String(n.length() ? " · " : "") + "skinut";
  if (s.online && s.batteryLow) n += String(n.length() ? " · " : "") + "slaba baterija";
  return n;
}
