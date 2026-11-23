#include "model/device_store.h"

#include <algorithm>

#include "config.h"

DeviceStore g_store;

// Posle uspešne komande kratko ignorišemo osvežavanja koja možda još nose staro stanje.
// Provera stvarnog stanja (CONFIRM_AFTER_CMD_MS) dolazi posle ovog perioda.
static constexpr uint32_t HOLD_AFTER_CMD_MS = 3000;

void DeviceStore::begin() { mutex_ = xSemaphoreCreateMutex(); }

// Primeni NAME_OVERRIDES iz config.h. Vraća false ako prekidač treba sakriti.
static bool applyOverride(SwitchInfo& s) {
  const String k = s.key();
  for (const NameOverride* o = NAME_OVERRIDES; o->key != nullptr; o++) {
    if (k == o->key) {
      if (o->name == nullptr || o->name[0] == '\0') return false;
      s.name = o->name;
      return true;
    }
  }
  return true;
}

static String foldForSort(const String& s) {
  String r = s;
  r.toLowerCase();
  return r;
}

void DeviceStore::sortLocked() {
  std::stable_sort(items_.begin(), items_.end(), [](const SwitchState& a, const SwitchState& b) {
    const String fa = foldForSort(a.name), fb = foldForSort(b.name);
    if (fa != fb) return fa < fb;
    return a.key() < b.key();
  });
}

void DeviceStore::replaceCloud(CloudKind cloud, std::vector<SwitchInfo>& fresh) {
  const uint32_t now = millis();
  std::vector<SwitchState> next;
  next.reserve(items_.size() + fresh.size());

  lock();
  // Zadrži prekidače drugog clouda
  for (auto& it : items_) {
    if (it.cloud != cloud) next.push_back(it);
  }

  bool layoutChanged = false;
  size_t oldCount = 0;
  for (auto& it : items_) if (it.cloud == cloud) oldCount++;

  size_t newCount = 0;
  for (auto& f : fresh) {
    if (!applyOverride(f)) continue;
    newCount++;
    SwitchState st;
    static_cast<SwitchInfo&>(st) = f;

    // Prenesi lokalno stanje (komanda u toku / sveže potvrđena komanda)
    auto old = std::find_if(items_.begin(), items_.end(),
                            [&](const SwitchState& o) { return o.cloud == cloud && o.key() == st.key(); });
    if (old == items_.end()) {
      layoutChanged = true;
    } else {
      if (old->name != st.name) layoutChanged = true;
      if (old->pending || (int32_t)(old->holdUntil - now) > 0) {
        st.on = old->on;
        st.pending = old->pending;
        st.holdUntil = old->holdUntil;
      }
    }
    next.push_back(st);
  }
  if (newCount != oldCount) layoutChanged = true;

  items_.swap(next);
  sortLocked();  // uvek: "next" je u redosledu iz API-ja, a UI poredi redosled ključeva
  if (layoutChanged) layoutRevision_++;
  revision_++;
  unlock();

  if (layoutChanged) {
    Serial.printf("[store] %s: %u prekidača\n", cloudName(cloud), (unsigned)newCount);
    for (auto& f : fresh) {
      Serial.printf("[store]   %-40s  %-28s  %-10s %s%s\n", f.key().c_str(), f.name.c_str(), groupName(f.group),
                    f.on ? "ON " : "OFF", f.online ? "" : "  (offline)");
    }
  }
}

bool DeviceStore::beginCommand(const String& key, bool target) {
  bool ok = false;
  lock();
  for (auto& it : items_) {
    if (it.key() == key) {
      if (it.online && !it.pending) {
        it.pending = true;
        it.on = target;  // optimistički prikaz
        ok = true;
        revision_++;
      }
      break;
    }
  }
  unlock();
  return ok;
}

void DeviceStore::commandFinished(const String& key, bool ok, bool target) {
  lock();
  for (auto& it : items_) {
    if (it.key() == key) {
      it.pending = false;
      it.on = ok ? target : !target;
      it.holdUntil = ok ? millis() + HOLD_AFTER_CMD_MS : 0;
      revision_++;
      break;
    }
  }
  unlock();
}

bool DeviceStore::findByKey(const String& key, SwitchInfo& out) {
  bool found = false;
  lock();
  for (auto& it : items_) {
    if (it.key() == key) {
      out = it;
      found = true;
      break;
    }
  }
  unlock();
  return found;
}

void DeviceStore::setWifi(bool connected, const String& ip) {
  lock();
  if (sys_.wifiConnected != connected || sys_.ip != ip) {
    sys_.wifiConnected = connected;
    sys_.ip = ip;
    revision_++;
  }
  unlock();
}

void DeviceStore::setTimeSynced(bool synced) {
  lock();
  if (sys_.timeSynced != synced) {
    sys_.timeSynced = synced;
    revision_++;
  }
  unlock();
}

void DeviceStore::setCloudStatus(CloudKind cloud, LinkState st, const String& msg) {
  lock();
  CloudStatus& cs = cloud == CloudKind::Tuya ? sys_.tuya : sys_.ewelink;
  if (cs.state != st || cs.message != msg) {
    cs.state = st;
    cs.message = msg;
    if (st == LinkState::Ok || st == LinkState::Error) sys_.initialLoadDone = true;
    revision_++;
  }
  unlock();
}

void DeviceStore::postToast(const String& msg) {
  lock();
  sys_.toast = msg;
  sys_.toastSeq++;
  revision_++;
  unlock();
}

// ---------------------------------------------------------------- alarm

static uint32_t exitDeadline(const AlarmInfo& a) {
  return a.exitDelay ? (millis() + a.exitDelay * 1000UL) | 1 : 0;  // | 1: 0 znači "nema"
}

void DeviceStore::replaceAlarms(const std::vector<AlarmInfo>& fresh) {
  const uint32_t now = millis();
  std::vector<AlarmState> next;
  bool layoutChanged = false;
  lock();
  for (const AlarmInfo& f : fresh) {
    AlarmState st;
    auto old = std::find_if(alarms_.begin(), alarms_.end(), [&](const AlarmState& a) { return a.deviceId == f.deviceId; });
    if (old == alarms_.end()) {
      layoutChanged = true;
      static_cast<AlarmInfo&>(st) = f;
    } else {
      st = *old;
      const AlarmMode prevMode = old->mode;
      const bool held = st.pending || (int32_t)(st.holdUntil - now) > 0;
      static_cast<AlarmInfo&>(st) = f;
      if (held) st.mode = prevMode;  // ne vraćaj optimistički prikaz na staro stanje
      // Uključen iz aplikacije/tastature (ne sa panela): odbrojavanje od trenutka kad smo to videli
      if (!held && !alarmArmed(prevMode) && alarmArmed(st.mode) && prevMode != AlarmMode::Unknown)
        st.exitUntil = exitDeadline(st);
      if (old->name != f.name) layoutChanged = true;
    }
    if (!alarmArmed(st.mode)) st.exitUntil = 0;
    if (st.exitUntil && (int32_t)(st.exitUntil - now) <= 0) st.exitUntil = 0;
    next.push_back(st);
  }
  if (next.size() != alarms_.size()) layoutChanged = true;
  std::sort(next.begin(), next.end(), [](const AlarmState& a, const AlarmState& b) {
    return foldForSort(a.name) < foldForSort(b.name);
  });
  alarms_.swap(next);
  if (layoutChanged) layoutRevision_++;
  revision_++;
  unlock();
}

bool DeviceStore::beginAlarmCommand(const String& deviceId, AlarmMode target) {
  bool ok = false;
  lock();
  for (auto& a : alarms_) {
    if (a.deviceId != deviceId) continue;
    if (a.online && !a.pending) {
      a.pending = true;
      a.mode = target;  // optimistički prikaz
      a.exitUntil = alarmArmed(target) ? exitDeadline(a) : 0;
      if (target == AlarmMode::Disarmed) a.alarming = false;
      ok = true;
      revision_++;
    }
    break;
  }
  unlock();
  return ok;
}

void DeviceStore::alarmCommandFinished(const String& deviceId, bool ok, AlarmMode target) {
  lock();
  for (auto& a : alarms_) {
    if (a.deviceId != deviceId) continue;
    a.pending = false;
    if (ok) {
      a.holdUntil = millis() + HOLD_AFTER_CMD_MS;
    } else {
      a.holdUntil = 0;
      a.exitUntil = 0;
      a.mode = AlarmMode::Unknown;  // pravo stanje stiže sa sledećim osvežavanjem
    }
    (void)target;
    revision_++;
    break;
  }
  unlock();
}

bool DeviceStore::findAlarm(const String& deviceId, AlarmInfo& out) {
  bool found = false;
  lock();
  for (auto& a : alarms_) {
    if (a.deviceId == deviceId) {
      out = a;
      found = true;
      break;
    }
  }
  unlock();
  return found;
}

void DeviceStore::replaceSensors(std::vector<SensorInfo> fresh) {
  std::sort(fresh.begin(), fresh.end(), [](const SensorInfo& a, const SensorInfo& b) {
    return foldForSort(a.name) < foldForSort(b.name);
  });
  lock();
  bool layoutChanged = fresh.size() != sensors_.size();
  bool changed = layoutChanged;
  for (size_t i = 0; !layoutChanged && i < fresh.size(); i++) {
    const SensorInfo &a = fresh[i], &b = sensors_[i];
    if (a.deviceId != b.deviceId || a.name != b.name) layoutChanged = changed = true;
    else if (a.online != b.online || a.hasValue != b.hasValue || a.value != b.value || a.fahrenheit != b.fahrenheit)
      changed = true;
  }
  if (changed) {
    sensors_.swap(fresh);
    revision_++;
  }
  if (layoutChanged) layoutRevision_++;
  unlock();
}

// ---------------------------------------------------------------- roletne

void DeviceStore::replaceCovers(const std::vector<CoverInfo>& fresh) {
  const uint32_t now = millis();
  std::vector<CoverState> next;
  lock();
  bool layoutChanged = fresh.size() != covers_.size();
  bool changed = layoutChanged;
  for (const CoverInfo& f : fresh) {
    CoverState st;
    static_cast<CoverInfo&>(st) = f;
    auto old = std::find_if(covers_.begin(), covers_.end(), [&](const CoverState& c) { return c.key() == f.key(); });
    if (old == covers_.end()) {
      layoutChanged = changed = true;
    } else {
      if (old->name != f.name) layoutChanged = true;
      if (old->pending || (int32_t)(old->holdUntil - now) > 0) {
        st.pending = old->pending;
        st.holdUntil = old->holdUntil;
        st.lastAction = old->lastAction;  // optimistički prikaz komande
      }
      if (old->name != st.name || old->online != st.online || old->position != st.position ||
          old->lastAction != st.lastAction || old->pending != st.pending)
        changed = true;
    }
    next.push_back(st);
  }
  std::sort(next.begin(), next.end(), [](const CoverState& a, const CoverState& b) {
    const String fa = foldForSort(a.name), fb = foldForSort(b.name);
    return fa != fb ? fa < fb : a.key() < b.key();
  });
  if (!layoutChanged) {
    for (size_t i = 0; i < next.size(); i++)
      if (next[i].key() != covers_[i].key()) layoutChanged = changed = true;
  }
  if (changed) {
    covers_.swap(next);
    revision_++;
  }
  if (layoutChanged) layoutRevision_++;
  unlock();
}

bool DeviceStore::beginCoverCommand(const String& key, CoverAction a) {
  bool ok = false;
  lock();
  for (auto& c : covers_) {
    if (c.key() != key) continue;
    if (c.online && (!c.pending || a == CoverAction::Stop)) {  // Stop uvek prolazi
      c.pending = true;
      c.lastAction = coverActionToTuya(a);
      ok = true;
      revision_++;
    }
    break;
  }
  unlock();
  return ok;
}

void DeviceStore::coverCommandFinished(const String& key, bool ok) {
  lock();
  for (auto& c : covers_) {
    if (c.key() != key) continue;
    c.pending = false;
    c.holdUntil = ok ? millis() + HOLD_AFTER_CMD_MS : 0;
    if (!ok) c.lastAction = "";
    revision_++;
    break;
  }
  unlock();
}

bool DeviceStore::findCover(const String& key, CoverInfo& out) {
  bool found = false;
  lock();
  for (auto& c : covers_) {
    if (c.key() == key) {
      out = c;
      found = true;
      break;
    }
  }
  unlock();
  return found;
}

std::vector<CoverState> DeviceStore::covers() {
  lock();
  std::vector<CoverState> copy = covers_;
  unlock();
  return copy;
}

// ---------------------------------------------------------------- detektori dima

void DeviceStore::replaceSmokes(std::vector<SmokeInfo> fresh) {
  std::sort(fresh.begin(), fresh.end(), [](const SmokeInfo& a, const SmokeInfo& b) {
    return foldForSort(a.name) < foldForSort(b.name);
  });
  lock();
  bool layoutChanged = fresh.size() != smokes_.size();
  bool changed = layoutChanged;
  for (size_t i = 0; !layoutChanged && i < fresh.size(); i++) {
    const SmokeInfo &a = fresh[i], &b = smokes_[i];
    if (a.deviceId != b.deviceId || a.name != b.name) layoutChanged = changed = true;
    else if (a.online != b.online || a.alarm != b.alarm || a.battery != b.battery || a.batteryLow != b.batteryLow ||
             a.tamper != b.tamper || a.fault != b.fault)
      changed = true;
  }
  if (changed) {
    smokes_.swap(fresh);
    revision_++;
  }
  if (layoutChanged) layoutRevision_++;
  unlock();
}

std::vector<SmokeInfo> DeviceStore::smokes() {
  lock();
  std::vector<SmokeInfo> copy = smokes_;
  unlock();
  return copy;
}

std::vector<SensorInfo> DeviceStore::sensors() {
  lock();
  std::vector<SensorInfo> copy = sensors_;
  unlock();
  return copy;
}

std::vector<AlarmState> DeviceStore::alarms() {
  lock();
  std::vector<AlarmState> copy = alarms_;
  unlock();
  return copy;
}

std::vector<SwitchState> DeviceStore::snapshot() {
  lock();
  std::vector<SwitchState> copy = items_;
  unlock();
  return copy;
}

SystemStatus DeviceStore::systemStatus() {
  lock();
  SystemStatus copy = sys_;
  unlock();
  return copy;
}

// ---------------------------------------------------------------- termostati

void DeviceStore::replaceThermostats(const std::vector<ThermostatInfo>& fresh) {
  const uint32_t now = millis();
  std::vector<ThermostatState> next;
  lock();
  bool layoutChanged = fresh.size() != thermostats_.size();
  bool changed = layoutChanged;
  for (const ThermostatInfo& f : fresh) {
    ThermostatState st;
    static_cast<ThermostatInfo&>(st) = f;
    auto old = std::find_if(thermostats_.begin(), thermostats_.end(),
                            [&](const ThermostatState& t) { return t.key() == f.key(); });
    if (old == thermostats_.end()) {
      layoutChanged = changed = true;
    } else {
      if (old->name != f.name) layoutChanged = true;
      if (old->pending || (int32_t)(old->holdUntil - now) > 0) {
        // Poslata vrednost važi dok uređaj ne javi novu (osvežavanje može doneti staro stanje)
        st.pending = old->pending;
        st.holdUntil = old->holdUntil;
        st.on = old->on;
        st.target = old->target;
        st.hasTarget = old->hasTarget;
      }
      if (old->name != st.name || old->online != st.online || old->on != st.on || old->target != st.target ||
          old->current != st.current || old->hasCurrent != st.hasCurrent || old->heating != st.heating ||
          old->mode != st.mode || old->pending != st.pending || old->minT != st.minT || old->maxT != st.maxT)
        changed = true;
    }
    next.push_back(st);
  }
  std::sort(next.begin(), next.end(), [](const ThermostatState& a, const ThermostatState& b) {
    return foldForSort(a.name) < foldForSort(b.name);
  });
  if (!layoutChanged) {
    for (size_t i = 0; i < next.size(); i++)
      if (next[i].key() != thermostats_[i].key()) layoutChanged = changed = true;
  }
  if (changed) {
    thermostats_.swap(next);
    revision_++;
  }
  if (layoutChanged) layoutRevision_++;
  unlock();
}

bool DeviceStore::beginThermoCommand(const String& key, int8_t power, float target) {
  bool ok = false;
  lock();
  for (auto& t : thermostats_) {
    if (t.key() != key) continue;
    if (t.online) {
      t.pending = true;
      if (power >= 0) t.on = power > 0;
      if (!isnan(target)) {
        t.target = target;
        t.hasTarget = true;
      }
      ok = true;
      revision_++;
    }
    break;
  }
  unlock();
  return ok;
}

void DeviceStore::thermoCommandFinished(const String& key, bool ok) {
  lock();
  for (auto& t : thermostats_) {
    if (t.key() != key) continue;
    t.pending = false;
    // Uspeh: prikazana vrednost važi još malo. Greška: sledeće osvežavanje vraća stvarno stanje.
    t.holdUntil = ok ? millis() + HOLD_AFTER_CMD_MS : 0;
    revision_++;
    break;
  }
  unlock();
}

bool DeviceStore::findThermostat(const String& key, ThermostatInfo& out) {
  bool found = false;
  lock();
  for (auto& t : thermostats_) {
    if (t.key() == key) {
      out = t;
      found = true;
      break;
    }
  }
  unlock();
  return found;
}

std::vector<ThermostatState> DeviceStore::thermostats() {
  lock();
  std::vector<ThermostatState> copy = thermostats_;
  unlock();
  return copy;
}

// ---------------------------------------------------------------- senzori vrata i pokreta

void DeviceStore::replaceBinaries(const std::vector<BinarySensorInfo>& fresh) {
  const time_t now = time(nullptr);
  const bool clockOk = now > 1700000000;
  std::vector<BinarySensorState> next;
  lock();
  bool layoutChanged = fresh.size() != binaries_.size();
  bool changed = layoutChanged;
  for (const BinarySensorInfo& f : fresh) {
    BinarySensorState st;
    static_cast<BinarySensorInfo&>(st) = f;
    auto old = std::find_if(binaries_.begin(), binaries_.end(),
                            [&](const BinarySensorState& b) { return b.key() == f.key(); });
    if (old == binaries_.end()) {
      layoutChanged = changed = true;
      if (f.active && clockOk) st.lastActive = now;
    } else {
      st.changedAt = old->changedAt;
      st.lastActive = old->lastActive;
      if (clockOk && f.online && old->online && f.active != old->active) st.changedAt = now;
      if (clockOk && f.active) st.lastActive = now;
      if (old->name != f.name || old->kind != f.kind) layoutChanged = true;
      if (old->name != st.name || old->online != st.online || old->active != st.active || old->battery != st.battery ||
          old->batteryLow != st.batteryLow || old->tamper != st.tamper || old->changedAt != st.changedAt ||
          (st.active && old->lastActive / 60 != st.lastActive / 60))  // pokret: vreme u minutima
        changed = true;
    }
    next.push_back(st);
  }
  std::sort(next.begin(), next.end(), [](const BinarySensorState& a, const BinarySensorState& b) {
    if (a.kind != b.kind) return a.kind < b.kind;  // prvo vrata, pa pokret
    return foldForSort(a.name) < foldForSort(b.name);
  });
  if (!layoutChanged) {
    for (size_t i = 0; i < next.size(); i++)
      if (next[i].key() != binaries_[i].key()) layoutChanged = changed = true;
  }
  binaries_.swap(next);  // uvek (vremena pokreta), a novo iscrtavanje samo kad se nešto vidljivo promeni
  if (changed) revision_++;
  if (layoutChanged) layoutRevision_++;
  unlock();
}

std::vector<BinarySensorState> DeviceStore::binaries() {
  lock();
  std::vector<BinarySensorState> copy = binaries_;
  unlock();
  return copy;
}
