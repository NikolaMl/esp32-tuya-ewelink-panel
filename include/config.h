#pragma once
// ============================================================================
//  Podešavanja panela (nisu tajna). Lozinke i ključevi su u include/secrets.h
// ============================================================================

#include <stdint.h>

// ---------------------------------------------------------------- Ekran / ploča
// Guition JC8048W550 / Sunton ESP32-8048S050 (ST7262, 800x480 RGB565)
#define LCD_H_RES            800
#define LCD_V_RES            480
// 14 MHz (~34 Hz osvežavanje): manje opterećuje PSRAM nego fabričkih 16 MHz, pa slika ne "puca"
// kad Wi-Fi i iscrtavanje rade istovremeno. Može da se proba uživo debug komandom "pclk N" (MHz).
#define LCD_PCLK_HZ          (14 * 1000 * 1000)
#define LCD_HSYNC_PULSE      4
#define LCD_HSYNC_BACK       8
#define LCD_HSYNC_FRONT      8
#define LCD_VSYNC_PULSE      4
#define LCD_VSYNC_BACK       8
#define LCD_VSYNC_FRONT      8
#define LCD_PCLK_ACTIVE_NEG  1

#define PIN_LCD_DE     40
#define PIN_LCD_VSYNC  41
#define PIN_LCD_HSYNC  39
#define PIN_LCD_PCLK   42
// Redosled podataka za RGB565: B0..B4, G0..G5, R0..R4
#define PIN_LCD_DATA { 8, 3, 46, 9, 1,   5, 6, 7, 15, 16, 4,   45, 48, 47, 21, 14 }
#define PIN_LCD_BL     2

// GT911 touch (I2C)
#define PIN_TOUCH_SDA  19
#define PIN_TOUCH_SCL  20
#define PIN_TOUCH_RST  38
#define PIN_TOUCH_INT  -1

// ---------------------------------------------------------------- Pozadinsko svetlo
#define BACKLIGHT_FULL        255   // 0..255
#define BACKLIGHT_DIM         25    // jačina kad je ekran "u mirovanju"
#define SCREEN_DIM_AFTER_MS   (15UL * 1000)       // zatamni posle 15 s bez dodira (upozorenje)
#define SCREEN_OFF_AFTER_MS   (20UL * 1000)       // ugasi posle 20 s (0 = nikad); dodir ga pali
// Dok su otvorena podešavanja (kucanje ključeva, QR za eWeLink) rok je duži
#define SETTINGS_DIM_AFTER_MS (110UL * 1000)
#define SETTINGS_OFF_AFTER_MS (120UL * 1000)

// ---------------------------------------------------------------- Osvežavanje stanja
// Tuya besplatni (trial) nalog ima mesečni limit API poziva, zato štedimo:
//  - dok se ekran koristi: često osvežavanje
//  - dok miruje: retko osvežavanje (+ odmah osveži čim ga dodirneš)
#define POLL_ACTIVE_MS        (10UL * 1000)
#define POLL_IDLE_MS          (5UL * 60 * 1000)
#define ACTIVE_WINDOW_MS      (90UL * 1000)  // koliko dugo posle dodira važi "aktivno"
#define CONFIRM_AFTER_CMD_MS  4000           // provera stvarnog stanja posle komande

// ---------------------------------------------------------------- Tuya
// Data centar tvog Tuya IoT projekta (za Srbiju je obično Central Europe):
//   Central Europe:  https://openapi.tuyaeu.com
//   Western Europe:  https://openapi-weaz.tuyaeu.com
//   Western America: https://openapi.tuyaus.com
//   China:           https://openapi.tuyacn.com
//   India:           https://openapi.tuyain.com
#define TUYA_ENDPOINT "https://openapi.tuyaeu.com"

// Tuya kategorije čiji se prekidači (DP switch / switch_N / switch_led) prikazuju; grupa u listi je
// u src/model/device_group.cpp. Roletne (cl, clkg), alarm (mal) i senzori temperature se prepoznaju sami.
//   svetla:    dj sijalica, dd LED traka, xdd plafonjera, fwd ambijentalno, dc dekorativna, tgq dimer,
//              tgkg dimer prekidač, gyd sa senzorom, sxd spot, fsd ventilator sa svetlom
//   prekidači: kg zidni, tdq modul, dlq osigurač, cjkg scenski, ckqdkg na karticu
//   utičnice:  cz utičnica, pc produžni kabl, wkcz sa termostatom
//   ostalo:    szjqr fingerbot, sfkzq ventil za vodu
// Termostati (wk) i klima uređaji namerno nisu tu (samo temperatura), da se grejanje ne ugasi slučajno.
#define TUYA_CATEGORIES "dj,dd,xdd,fwd,dc,tgq,tgkg,gyd,sxd,fsd,kg,tdq,dlq,cjkg,ckqdkg,cz,pc,wkcz,szjqr,sfkzq"

// ---------------------------------------------------------------- Vreme
#define NTP_SERVER_1   "pool.ntp.org"
#define NTP_SERVER_2   "time.google.com"
#define TIMEZONE       "CET-1CEST,M3.5.0,M10.5.0/3"   // Beograd

// ---------------------------------------------------------------- Prikaz
#define PANEL_TITLE    "Uređaji"

// Opciono: preimenuj ili sakrij pojedinačne prekidače.
//   key  = "<deviceId>" za jednokanalne ili "<deviceId>/<kanal>" za višekanalne
//          (Tuya kanal = DP kod, npr. "switch_2"; eWeLink kanal = broj od 0, npr. "1" = drugi taster)
//   name = novo ime, ili "" da se prekidač sakrije
// Tačne ključeve vidiš u Serial monitoru posle prvog učitavanja uređaja.
// ---------------------------------------------------------------- početni ekran (slika kuće)
// Šta je na slici i gde, podešava se sa telefona (QR: Podešavanja → Web → Raspored).
// Ovo je samo početni raspored (i "Podrazumevani raspored"): CENTAR oznake u pikselima ekrana (800x480).
// Početni ekran može imati više delova (svaki sa svojom fotografijom); listaju se prevlačenjem levo/desno.
#define HOME_MAX_PAGES 6
struct HomePos { const char* key; int16_t x, y; };
// Lični raspored drži u include/home_positions.h (u .gitignore, ne ide na GitHub); format je isti:
#if __has_include("home_positions.h")
#include "home_positions.h"
#else
static const HomePos HOME_POSITIONS[] = {
    // { "bf0123456789abcdef", 358, 150 },  // npr. senzor temperature — prozor spavaće sobe
    { nullptr, 0, 0 }
};
#endif

struct NameOverride { const char* key; const char* name; };
static const NameOverride NAME_OVERRIDES[] = {
    // { "bf0123456789abcdef/switch_2", "Hodnik" },
    // { "1000a1b2c3/0", "" },          // sakrij
    { nullptr, nullptr }
};
