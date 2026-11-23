#pragma once
#include <Arduino.h>

// Senzor temperature (Tuya "wsdcg" i drugi uređaji koji javljaju trenutnu temperaturu).
struct SensorInfo {
  String deviceId;
  String name;
  bool online = false;
  bool hasValue = false;
  float value = 0;          // u jedinici uređaja (°C ili °F)
  bool fahrenheit = false;

  // Ključ za sakrivanje (isti prostor ključeva kao prekidači)
  String key() const { return deviceId; }
};
