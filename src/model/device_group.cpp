#include "model/device_group.h"

#include <string.h>

// Tuya standardne kategorije (developer.tuya.com -> Standard Instruction Set), grupisane za prikaz.
// Roletne (cl, clkg), alarm centrale (mal) i senzori temperature imaju svoje sekcije u listi.
struct TuyaCat {
  const char* code;
  DeviceGroup group;
};

static const TuyaCat TUYA_CATS[] = {
    // Osvetljenje
    {"dj", DeviceGroup::Lights},     // sijalica
    {"dd", DeviceGroup::Lights},     // LED traka
    {"xdd", DeviceGroup::Lights},    // plafonjera
    {"fwd", DeviceGroup::Lights},    // ambijentalno svetlo
    {"dc", DeviceGroup::Lights},     // dekorativna svetla
    {"tgq", DeviceGroup::Lights},    // dimer
    {"tgkg", DeviceGroup::Lights},   // dimer prekidač
    {"gyd", DeviceGroup::Lights},    // svetlo sa senzorom pokreta
    {"sxd", DeviceGroup::Lights},    // spot svetlo
    {"tyndj", DeviceGroup::Lights},  // solarno svetlo
    {"fsd", DeviceGroup::Lights},    // ventilator sa svetlom
    {"tyd", DeviceGroup::Lights},    // svetlo za kretanje
    // Prekidači
    {"kg", DeviceGroup::Switches},      // zidni prekidač
    {"tdq", DeviceGroup::Switches},     // prekidač / modul
    {"dlq", DeviceGroup::Switches},     // osigurač (pametni)
    {"cjkg", DeviceGroup::Switches},    // scenski prekidač
    {"ckqdkg", DeviceGroup::Switches},  // prekidač na karticu
    {"wxkg", DeviceGroup::Switches},    // bežični prekidač
    // Utičnice
    {"cz", DeviceGroup::Sockets},    // utičnica
    {"pc", DeviceGroup::Sockets},    // produžni kabl
    {"wkcz", DeviceGroup::Sockets},  // utičnica sa termostatom
    // Klima i grejanje
    {"wk", DeviceGroup::Climate},     // termostat
    {"wkf", DeviceGroup::Climate},    // termostatski ventil radijatora
    {"wsdcg", DeviceGroup::Climate},  // senzor temperature i vlažnosti
    {"kt", DeviceGroup::Climate},     // klima uređaj
    {"ktkzq", DeviceGroup::Climate},  // upravljač klime
    {"qn", DeviceGroup::Climate},     // grejalica
    {"fs", DeviceGroup::Climate},     // ventilator
    {"fskg", DeviceGroup::Climate},   // prekidač ventilatora
    {"jsq", DeviceGroup::Climate},    // ovlaživač
    {"cs", DeviceGroup::Climate},     // odvlaživač
    {"kj", DeviceGroup::Climate},     // prečišćivač vazduha
    {"xfj", DeviceGroup::Climate},    // ventilacija
    {"rs", DeviceGroup::Climate},     // bojler
    {"bh", DeviceGroup::Climate},     // kuvalo / bojler
    // Sigurnost
    {"mal", DeviceGroup::Security},     // alarm centrala
    {"ywbj", DeviceGroup::Security},    // detektor dima
    {"rqbj", DeviceGroup::Security},    // detektor gasa
    {"cobj", DeviceGroup::Security},    // detektor CO
    {"sj", DeviceGroup::Security},      // senzor curenja vode
    {"mcs", DeviceGroup::Security},     // senzor vrata/prozora
    {"pir", DeviceGroup::Security},     // senzor pokreta
    {"hps", DeviceGroup::Security},     // senzor prisustva
    {"sgbj", DeviceGroup::Security},    // sirena
    {"sp", DeviceGroup::Security},      // kamera
    {"ms", DeviceGroup::Security},      // brava
    {"jtmspro", DeviceGroup::Security}, // brava
    {"sos", DeviceGroup::Security},     // SOS taster
    // Ostalo (npr. szjqr = fingerbot, sfkzq = ventil za vodu, cwwsq = hranilica, ckmkzq = garažna vrata)
};

DeviceGroup groupForTuyaCategory(const String& category) {
  for (const TuyaCat& c : TUYA_CATS)
    if (category == c.code) return c.group;
  return DeviceGroup::Other;
}

// eWeLink (Sonoff) UIID: osvetljenje i utičnice; ostalo su prekidači (Basic, Mini, zidni T-serija, Dual...)
DeviceGroup groupForEwelinkUiid(int uiid) {
  switch (uiid) {
    case 22: case 36: case 44: case 57: case 59: case 103: case 104: case 135: case 136: case 137: case 173:
      return DeviceGroup::Lights;  // B1/B02/B05 sijalice, D1 dimer, L1/L2 trake, KING-M4 dimer
    case 5: case 32: case 182: case 190: case 262: case 276: case 277:
      return DeviceGroup::Sockets;  // POW, S26/S40 utičnice, POWR3...
    case 15: case 181:
      return DeviceGroup::Climate;  // TH / THR3 (prekidač sa senzorom temperature)
    default:
      return DeviceGroup::Switches;
  }
}
