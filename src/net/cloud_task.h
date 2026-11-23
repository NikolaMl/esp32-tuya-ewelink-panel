#pragma once
#include <Arduino.h>

#include <vector>

#include "model/alarm.h"
#include "model/cover.h"

// Mrežni task (core 0): Wi-Fi, NTP, periodično učitavanje stanja i izvršavanje komandi.
// UI task nikad ne čeka mrežu — samo ubacuje zahteve.
namespace cloud {

struct WifiNetwork {
  String ssid;
  int32_t rssi;
  bool secure;
};

void begin();

// Iz UI taska:
bool requestSet(const String& key, bool target);  // false = red je pun / prekidač zauzet
bool requestAlarm(const String& deviceId, AlarmMode mode);  // false = centrala zauzeta/offline
bool requestCover(const String& key, CoverAction a);        // false = roletna zauzeta/offline
// Termostat: power -1 = ne menja (0/1 = isključi/uključi), target NAN = ne menja. false = nedostupan.
bool requestThermostat(const String& key, int8_t power, float target);
void requestRefresh();                            // osveži odmah
void requestSmokeLog(const String& deviceId = "");  // debug: istorija detektora dima (ili jednog uređaja) u Serial
void notifyActivity();                            // korisnik je dodirnuo ekran

// Wi-Fi skeniranje (radi ga mrežni task, rezultat se čita kad scanSeq() poraste)
void requestWifiScan();
bool wifiScanning();
uint32_t scanSeq();
std::vector<WifiNetwork> scanResults();

// Za demo režim (debug konzola): komanda za prekidač čiji ključ počinje sa "demo"
// se ne šalje u cloud nego se lokalno potvrdi.
void setDemoMode(bool on);

}  // namespace cloud
