#pragma once
#include <Arduino.h>

// Grupe uređaja za prikaz (lista na panelu, telefon). Tuya kategorija / eWeLink UIID -> grupa.
// Redosled ovde je i redosled sekcija u listi.
enum class DeviceGroup : uint8_t { Lights, Switches, Sockets, Climate, Security, Other };

inline const char* groupName(DeviceGroup g) {
  switch (g) {
    case DeviceGroup::Lights: return "Svetla";
    case DeviceGroup::Switches: return "Prekidači";
    case DeviceGroup::Sockets: return "Utičnice";
    case DeviceGroup::Climate: return "Klima";
    case DeviceGroup::Security: return "Sigurnost";
    default: return "Ostalo";
  }
}

// Za telefon (web): kratka oznaka grupe
inline const char* groupId(DeviceGroup g) {
  switch (g) {
    case DeviceGroup::Lights: return "light";
    case DeviceGroup::Switches: return "switch";
    case DeviceGroup::Sockets: return "socket";
    case DeviceGroup::Climate: return "climate";
    case DeviceGroup::Security: return "security";
    default: return "other";
  }
}

// "Sve ugasi" i brojač na listi važe samo za svetla i zidne prekidače (ne utičnice, pumpe, grejače...)
inline bool groupIsLighting(DeviceGroup g) { return g == DeviceGroup::Lights || g == DeviceGroup::Switches; }

DeviceGroup groupForTuyaCategory(const String& category);
DeviceGroup groupForEwelinkUiid(int uiid);
