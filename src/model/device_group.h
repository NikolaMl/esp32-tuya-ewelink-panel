#pragma once
#include <Arduino.h>

#include "util/i18n.h"

// Grupe uređaja za prikaz (lista na panelu, telefon). Tuya kategorija / eWeLink UIID -> grupa.
// Redosled ovde je i redosled sekcija u listi.
enum class DeviceGroup : uint8_t { Lights, Switches, Sockets, Climate, Security, Other };

inline const char* groupName(DeviceGroup g) {
  switch (g) {
    case DeviceGroup::Lights: return TR("Svetla", "Lights");
    case DeviceGroup::Switches: return TR("Prekidači", "Switches");
    case DeviceGroup::Sockets: return TR("Utičnice", "Sockets");
    case DeviceGroup::Climate: return TR("Klima", "Climate");
    case DeviceGroup::Security: return TR("Sigurnost", "Security");
    default: return TR("Ostalo", "Other");
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
