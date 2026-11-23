#pragma once
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

#include "model/settings.h"

// Početni ekran: slika kuće preko celog ekrana, sat, katanac (stanje alarma), temperature,
// svetla i roletne postavljeni na svoja mesta na slici. Dugi dodir na sliku = uređivanje rasporeda.
// Može imati više delova (npr. tri strane kuće) koji se menjaju prevlačenjem levo/desno.
namespace home_ui {

lv_obj_t* build();         // napravi ekran (ne učitava ga)
void update();             // iz ui::update(): stanje uređaja, sat, katanac
void setEditMode(bool on);
// Delovi početnog ekrana (svaki sa svojom fotografijom), listaju se prevlačenjem levo/desno
void setPage(uint8_t page);
uint8_t page();
// Debug (konzola "dt"): oznaka samo u RAM-u, na prikazanom delu (ne menja sačuvan raspored)
void debugPlace(const String& key, int16_t x, int16_t y);
// Demo prikaz (konzola "show"): samo ove oznake, prvi deo sa ugrađenom slikom; do restarta
void showcase(const std::vector<settings::Position>& pos);

}  // namespace home_ui
