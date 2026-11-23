#pragma once
#include <Arduino.h>

#include <vector>

#include "model/switch_info.h"

// Zajednički interfejs za Tuya i eWeLink.
class CloudProvider {
 public:
  virtual ~CloudProvider() = default;
  virtual CloudKind kind() const = 0;
  virtual bool configured() const = 0;
  // Učitaj sve prekidače sa trenutnim stanjem (jedan ili par API poziva)
  virtual bool fetch(std::vector<SwitchInfo>& out, String& err) = 0;
  // Uključi / isključi jedan prekidač
  virtual bool setSwitch(const SwitchInfo& sw, bool on, String& err) = 0;
};

// Trenutno vreme u milisekundama od 1970 (posle NTP sinhronizacije)
uint64_t epochMillis();
