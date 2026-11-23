#pragma once
#include <lvgl.h>
#include <stdint.h>

// RGB panel (ST7262 800x480) + LVGL 9 display + pozadinsko svetlo.
namespace display {

bool begin();                       // poziva i lv_init()
void setBacklight(uint8_t level);   // 0..255
lv_display_t* lv();
bool setPclk(uint32_t hz);          // promena takta uživo (debug)
void setRefreshLog(bool on);        // debug: svako iscrtavanje u log
bool refreshLog();
void shutdown();                    // pre restarta: ugasi svetlo i zaustavi LCD DMA (posle toga nema crtanja)

}  // namespace display
