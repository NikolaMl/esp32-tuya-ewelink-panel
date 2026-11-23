#pragma once
#include <Arduino.h>
#include <lvgl.h>

namespace ui {

void begin(lv_display_t* disp);  // napravi ekrane (posle display::begin)
void update();                   // pozivati iz loop(): sinhronizuje ekran sa stanjem
lv_obj_t* mainScreen();          // početni ekran (slika kuće)
void openList();                 // ekran sa svim pločicama (svetla, alarm, senzori)
void scrollList(int32_t y);      // konzola: pomeri listu na y (za snimke ekrana)
void goHome();                   // zatvori sve (podešavanja, alarm, listu, uređivanje) -> početni ekran
void toggleSwitch(const String& key);  // dodir na svetlo (poruka ako je nedostupno)
void showToast(const String& text, bool error = true);  // kratka poruka preko bilo kog ekrana
void runScene(const String& id);       // pokreni scenu (PIN ako treba isključiti alarm)

}  // namespace ui
