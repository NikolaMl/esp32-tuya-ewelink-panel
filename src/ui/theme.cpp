#include "ui/theme.h"

#include <Arduino.h>

extern "C" const uint8_t montserrat_semibold_ttf[];
extern "C" const size_t montserrat_semibold_ttf_len;

namespace theme {

lv_color_t C_BG, C_CARD, C_CARD_OFFLINE, C_ON, C_TEXT, C_MUTED, C_ON_TEXT, C_ON_MUTED, C_OK, C_WARN, C_ERR,
    C_TOAST, C_INPUT;
lv_font_t* F_SMALL = nullptr;
lv_font_t* F_BODY = nullptr;
lv_font_t* F_NAME = nullptr;
lv_font_t* F_TITLE = nullptr;
lv_font_t* F_HUGE = nullptr;

static lv_font_t* makeFont(int size, const lv_font_t* fallback) {
  // Bez kerninga: TinyTTF (LVGL 9.2) računa kerning za svaki par znakova van keša,
  // a Montserrat ima veliku GPOS tabelu -> izgradnja ekrana je trajala ~8 s umesto <1 s.
  lv_font_t* f = lv_tiny_ttf_create_data_ex(montserrat_semibold_ttf, montserrat_semibold_ttf_len, size,
                                            LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT);
  if (!f) return const_cast<lv_font_t*>(fallback);
  // Rezerva za znakove kojih nema u TTF-u (LVGL 9.2 tada loguje zbunjujuće "cache not allocated")
  f->fallback = fallback;
  return f;
}

void init(lv_display_t* disp) {
  C_BG = lv_color_hex(0x0E1116);
  C_CARD = lv_color_hex(0x1B2129);
  C_CARD_OFFLINE = lv_color_hex(0x14181D);
  C_ON = lv_color_hex(0xF6B93B);
  C_TEXT = lv_color_hex(0xE8ECF1);
  C_MUTED = lv_color_hex(0x7D8896);
  C_ON_TEXT = lv_color_hex(0x231A00);
  C_ON_MUTED = lv_color_hex(0x6B5310);
  C_OK = lv_color_hex(0x3FB950);
  C_WARN = lv_color_hex(0xD29922);
  C_ERR = lv_color_hex(0xF85149);
  C_TOAST = lv_color_hex(0x3A1D1F);
  C_INPUT = lv_color_hex(0x0E1116);

  F_SMALL = makeFont(16, &lv_font_montserrat_16);
  F_BODY = makeFont(20, &lv_font_montserrat_20);
  F_NAME = makeFont(22, &lv_font_montserrat_24);
  F_TITLE = makeFont(30, &lv_font_montserrat_28);
  F_HUGE = makeFont(52, &lv_font_montserrat_40);

  lv_theme_t* th = lv_theme_default_init(disp, C_ON, C_OK, true, &lv_font_montserrat_20);
  lv_display_set_theme(disp, th);
}

}  // namespace theme
