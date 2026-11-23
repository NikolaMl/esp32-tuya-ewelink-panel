#pragma once
#include <lvgl.h>

// Ekran "Podešavanja": Wi-Fi, Tuya, eWeLink, Uređaji (prikaži/sakrij), Alarm (PIN), Web.
namespace settings_ui {

enum Tab { TAB_WIFI = 0, TAB_TUYA = 1, TAB_EWELINK = 2, TAB_SWITCHES = 3, TAB_ALARM = 4, TAB_WEB = 5 };

void build(lv_obj_t* mainScreen);
void open(int tab);          // traži PIN alarma (ako je podešen) pre otvaranja
void openUnlocked(int tab);  // bez PIN-a (debug konzola preko USB-a)
void close();
bool isOpen();
void update();  // osvežava statuse dok je ekran otvoren

}  // namespace settings_ui
