/**
 * LVGL 9.2 konfiguracija za ESP32 Tuya & eWeLink Panel.
 * Sve opcije koje ovde nisu navedene uzimaju podrazumevane vrednosti iz lv_conf_internal.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Napomena: ovde ne uključivati C zaglavlja — lv_conf.h čitaju i LVGL .S (asembler) fajlovi. */

/* Boje */
#define LV_COLOR_DEPTH 16

/* Memorija: sve LVGL alokacije idu u PSRAM (src/ui/lv_mem_psram.c), interni RAM čuvamo za Wi-Fi/TLS */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* Bez OS integracije — LVGL radi samo iz jednog (UI) taska */
#define LV_USE_OS   LV_OS_NONE

/* Osvežavanje */
#define LV_DEF_REFR_PERIOD  16
#define LV_DPI_DEF          130

/* Logovanje (ide na Serial preko callback-a) */
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   0

/* Ugrađeni fontovi (ASCII + LVGL simboli); srpska slova dolaze iz generisanih fontova */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* TinyTTF: iscrtava ugrađeni Montserrat TTF (srpska latinica + ćirilica) u bilo kojoj veličini */
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 512

/* Tema */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Widgeti koje ne koristimo — manje koda */
#define LV_USE_CALENDAR 0
#define LV_USE_CHART    0
#define LV_USE_TABLE    0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0
#define LV_USE_MENU     0
#define LV_USE_SPAN     0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_ANIMIMG  0
#define LV_USE_LED      0
#define LV_USE_SCALE    0

/* Ekran za podešavanja */
#define LV_USE_KEYBOARD 1
#define LV_USE_TABVIEW  1
#define LV_USE_QRCODE   1   /* QR za eWeLink prijavu i web podešavanje */
#define LV_USE_SNAPSHOT 1   /* snimak ekrana preko serijskog porta (tools/screenshot.py) */

/* Layout-i */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif /* LV_CONF_H */
