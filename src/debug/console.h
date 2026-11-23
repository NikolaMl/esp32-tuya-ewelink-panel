#pragma once
#include <stdint.h>

// Debug komande preko USB serijskog porta (115200):
//   s        -> snimak ekrana (čita ga tools/screenshot.py)
//   d        -> demo prekidači (bez naloga, za probu izgleda)
//   t X Y    -> simuliran dodir na koordinatama X,Y
//   p N      -> otvori podešavanja na tabu N (0=Wi-Fi 1=Tuya 2=eWeLink 3=Web)
//   m        -> glavni ekran
namespace debug {

void poll();  // pozivati iz loop()
bool injectedTouch(int16_t& x, int16_t& y);

}  // namespace debug
