#pragma once
// ============================================================================
//  OPCIONO. Sve ovo možeš da podesiš i na samom panelu (ikonica zupčanika)
//  ili preko web stranice panela. Ako ipak želiš početne vrednosti u kodu:
//  kopiraj ovaj fajl u include/secrets.h i popuni ga.
//  Vrednosti sačuvane na panelu uvek imaju prednost.
//  secrets.h je u .gitignore — nikad ga ne deli i ne commit-uj.
// ============================================================================

// ---- Wi-Fi (2.4 GHz mreža; ESP32 ne podržava 5 GHz)
#define WIFI_SSID      ""
#define WIFI_PASSWORD  ""

// ---- Tuya / Smart Life  (iot.tuya.com -> Cloud -> tvoj projekat -> Overview)
#define TUYA_ACCESS_ID      ""
#define TUYA_ACCESS_SECRET  ""

// ---- eWeLink  (dev.ewelink.cc -> tvoja aplikacija)
// Prijava na eWeLink nalog se radi sa panela: Podešavanja -> eWeLink -> Poveži nalog
#define EWELINK_APP_ID      ""
#define EWELINK_APP_SECRET  ""
