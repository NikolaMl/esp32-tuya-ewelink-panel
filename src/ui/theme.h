#pragma once
#include <lvgl.h>

// Zajedničke boje i fontovi za sve ekrane.
namespace theme {

extern lv_color_t C_BG, C_CARD, C_CARD_OFFLINE, C_ON, C_TEXT, C_MUTED, C_ON_TEXT, C_ON_MUTED, C_OK, C_WARN,
    C_ERR, C_TOAST, C_INPUT;

// TinyTTF Montserrat (srpska latinica + ćirilica). LVGL simbole (LV_SYMBOL_*) NE stavljati
// u tekst sa ovim fontovima — za ikone koristiti lv_font_montserrat_* u posebnoj labeli.
extern lv_font_t* F_SMALL;  // 16 px
extern lv_font_t* F_BODY;   // 20 px
extern lv_font_t* F_NAME;   // 22 px
extern lv_font_t* F_TITLE;  // 30 px
extern lv_font_t* F_HUGE;   // 52 px, temperatura

void init(lv_display_t* disp);

}  // namespace theme
