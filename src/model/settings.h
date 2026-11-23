#pragma once
#include <Arduino.h>

#include <vector>

#include "util/i18n.h"

// Podešavanja koja korisnik menja sa ekrana ili preko web stranice.
// Čuvaju se u NVS memoriji; include/secrets.h (ako postoji) daje samo početne vrednosti.
struct Settings {
  String wifiSsid;
  String wifiPass;
  String tuyaEndpoint;
  String tuyaId;
  String tuyaSecret;
  String ewAppId;
  String ewAppSecret;
};

struct TuyaRegion {
  const char* label;
  const char* endpoint;
};
extern const TuyaRegion TUYA_REGIONS[];
extern const size_t TUYA_REGION_COUNT;

namespace settings {

void begin();
Settings get();
void setWifi(const String& ssid, const String& pass);
void setTuya(const String& endpoint, const String& id, const String& secret);
void setEWeLink(const String& appId, const String& appSecret);
// Signal da su eWeLink tokeni promenjeni (prijava/odjava) — mrežni task ih ponovo učitava
void touchEWeLinkTokens();

// Raste sa svakom promenom; mrežni task po tome zna da treba da primeni nova podešavanja
uint32_t revision();

// PIN za web podešavanje (nasumičan pri svakom pokretanju, prikazan na ekranu).
// Softverski restart (promena jezika) zadržava isti PIN, da telefon ostane prijavljen.
const String& webPin();

// Jezik interfejsa sačuvan u NVS-u (podrazumevano UI_DEFAULT_LANGUAGE iz config.h).
// Aktivni jezik je i18n::lang(); novi se primenjuje restartom (restart::soon).
Lang language();
bool setLanguage(Lang l);  // true = promenjen (treba restart)

// Prekidači sakriveni sa glavnog ekrana (ključ = SwitchInfo::key()). Čuva se u NVS-u.
// Ima svoj brojač izmena, da sakrivanje ne bi ponovo pokretalo Wi-Fi i cloud klijente.
bool isHidden(const String& key);
void setHidden(const String& key, bool hidden);
uint32_t hiddenRevision();

// Raspored oznaka (temperature, svetla) na slici početnog ekrana: centar oznake u pikselima.
// Svaka oznaka je poseban unos, pa isti uređaj (npr. svetlo) može biti na slici više puta.
// Bezbedno iz UI i web taska.
// style kod svetla: 0 = okruglo dugme, 1 = nevidljivo polje (npr. preko sijalice na fotografiji)
enum : uint8_t { STYLE_BUTTON = 0, STYLE_INVISIBLE = 1 };
struct Position {
  String key;
  int16_t x, y;
  uint8_t style = STYLE_BUTTON;
  uint8_t page = 0;  // deo početnog ekrana (fotografija), 0 = prvi
};
static constexpr size_t MAX_POSITIONS = 48;
// Pomeri oznaku br. index (redosled iz positions()); samo u RAM-u, izgled ostaje.
// Ključ mora da se poklapa, inače je raspored u međuvremenu zamenjen sa telefona -> false.
bool movePosition(size_t index, const String& key, int16_t x, int16_t y);
void savePositions();  // upiši u NVS (posle uređivanja)
std::vector<Position> positions();
// Na početnom ekranu je tačno ono što je u rasporedu. Sa telefona se šalje ceo raspored (zamena).
void replacePositions(const std::vector<Position>& list);
void resetPositions();  // nazad na HOME_POSITIONS iz config.h (bez sakrivenih), sve na prvom delu

// Delovi početnog ekrana: broj = names.size() (1..HOME_MAX_PAGES), naziv može biti prazan
std::vector<String> pageNames();
uint8_t pageCount();
bool addPage(const String& name);           // false = već ima HOME_MAX_PAGES delova
void renamePages(const std::vector<String>& names);  // samo nazivi postojećih delova
void deletePage(uint8_t page);              // briše oznake tog dela, kasniji delovi se pomeraju za jedan

// Ekran (menja se sa telefona; početne vrednosti iz config.h)
struct ScreenCfg {
  uint16_t dimSec;   // zatamni posle (s)
  uint16_t offSec;   // ugasi posle (s), 0 = nikad
};
ScreenCfg screen();
void setScreen(const ScreenCfg& c);

// Raste kad se sa telefona promeni raspored, ekran ili pozadina -> početni ekran se osvežava
uint32_t uiRevision();
void touchUi();

// PIN za isključivanje alarma sa panela (čuva se samo heš sa nasumičnom soli)
bool hasAlarmPin();
// Posle 5 pogrešnih pokušaja zaključavanje (30 s, pa duplo duže…) — važi za sve ekrane
enum class PinResult { Ok, Wrong, Locked };
PinResult verifyAlarmPin(const String& pin);
uint32_t alarmPinLockedMs();  // koliko još traje zaključavanje (0 = nije)
void setAlarmPin(const String& pin);

}  // namespace settings
