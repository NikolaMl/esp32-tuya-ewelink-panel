#pragma once
#include <Arduino.h>
#include <math.h>

// Termostat (Tuya "wk"), termostatska glava radijatora ("wkf") i drugi uređaji sa DP "temp_set"
// (grejalica, klima) koji nisu u listi prekidača. Panel prikazuje trenutnu i željenu temperaturu,
// pali/gasi ("switch") i menja željenu temperaturu. Režim (raspored / ručno) se samo prikazuje.
struct ThermostatInfo {
  String deviceId;
  String name;
  bool online = false;
  bool hasPower = false;   // ima DP "switch"
  bool on = true;          // bez DP "switch" uvek uključen
  bool hasCurrent = false;
  float current = 0;       // °C
  bool hasTarget = false;
  float target = 0;        // °C
  float minT = 5, maxT = 35, step = 0.5f;  // iz specifikacije uređaja (temp_set)
  int8_t heating = -1;     // 1 = greje, 0 = ne greje, -1 = uređaj ne javlja
  String mode;             // DP "mode" kako ga javlja uređaj (auto, manual, ...), prazno = nema

  // Ključ za raspored i sakrivanje (isti prostor ključeva kao ostali uređaji)
  String key() const { return deviceId; }
};

struct ThermostatState : ThermostatInfo {
  bool pending = false;    // komanda poslata, čeka se odgovor
  uint32_t holdUntil = 0;  // do tada važi poslata vrednost, a ne stara iz osvežavanja
};

// Zaokruži na korak uređaja i ograniči na opseg
inline float thermoClamp(const ThermostatInfo& t, float v) {
  const float st = t.step > 0 ? t.step : 0.5f;
  v = roundf(v / st) * st;
  if (v < t.minT) v = t.minT;
  if (v > t.maxT) v = t.maxT;
  return v;
}

// "21,5°" (decimalni zarez; celi stepeni bez decimale)
inline String thermoDegrees(float v) {
  char buf[16];
  if (fabsf(v - roundf(v)) < 0.05f) snprintf(buf, sizeof(buf), "%d°", (int)roundf(v));
  else snprintf(buf, sizeof(buf), "%.1f°", v);
  if (char* dot = strchr(buf, '.')) *dot = ',';
  return buf;
}

// Režim za prikaz; nepoznat se prikazuje kako ga uređaj javlja
inline String thermoModeText(const String& m) {
  if (m.isEmpty()) return "";
  if (m == "auto" || m == "program" || m == "schedule" || m == "smart") return "po rasporedu";
  if (m == "manual" || m == "hold" || m == "temp_manual") return "ručno";
  if (m == "eco") return "eko";
  if (m == "holiday" || m == "away") return "odmor";
  if (m == "comfort") return "komfor";
  if (m == "frost" || m == "antifreeze") return "zaštita od mraza";
  if (m == "hot" || m == "heat") return "grejanje";
  if (m == "cold" || m == "cool") return "hlađenje";
  if (m == "wind" || m == "fan") return "ventilator";
  if (m == "dry" || m == "dehumidification") return "sušenje";
  return m;
}

// Kratak opis stanja za listu i telefon: "21,5° → 22°", "Isključen", "Nedostupan"
inline String thermoStateText(const ThermostatInfo& t) {
  if (!t.online) return "Nedostupan";
  String s = t.hasCurrent ? thermoDegrees(t.current) : String("--");
  if (!t.on) return s + " · isključen";
  if (t.hasTarget) s += " → " + thermoDegrees(t.target);
  if (t.heating == 1) s += " · greje";
  return s;
}
