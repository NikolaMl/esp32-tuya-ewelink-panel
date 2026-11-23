#pragma once
#include <stdint.h>

// Minimalni GT911 drajver (I2C, bez INT pina — čita se periodično iz LVGL-a).
namespace touch {

bool begin();
// true dok je ekran pritisnut; x/y su u pikselima ekrana
bool read(int16_t& x, int16_t& y);

}  // namespace touch
