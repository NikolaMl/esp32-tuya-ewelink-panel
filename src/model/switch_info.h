#pragma once
#include <Arduino.h>

#include "model/device_group.h"

enum class CloudKind : uint8_t { Tuya = 0, EWeLink = 1 };

inline const char* cloudName(CloudKind c) { return c == CloudKind::Tuya ? "Tuya" : "eWeLink"; }

// Jedan prekidač (kanal) koji se prikazuje kao pločica na ekranu.
// Višekanalni uređaj (npr. trostruki prekidač) daje više SwitchInfo zapisa.
struct SwitchInfo {
  CloudKind cloud = CloudKind::Tuya;
  String deviceId;
  String channel;      // Tuya: DP kod ("switch_1"); eWeLink: "" (jednokanalni) ili "0".."3"
  String name;         // ime za prikaz
  bool on = false;
  bool online = true;
  DeviceGroup group = DeviceGroup::Switches;  // vrsta uređaja (sekcija u listi)

  // Jedinstveni ključ: "<deviceId>" ili "<deviceId>/<kanal>"
  String key() const { return channel.length() && isMultiChannel ? deviceId + "/" + channel : deviceId; }
  bool isMultiChannel = false;
};
