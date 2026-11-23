#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Prozorče za termostat (preko bilo kog ekrana): trenutna temperatura, željena sa − / +,
// uključi/isključi. Otvara se dodirom na termostat na početnom ekranu ili na pločicu u listi.
// Dodiri na − / + odmah menjaju prikaz; komanda ide 1,2 s posle poslednjeg dodira (jedna, ne pet).
namespace thermo_ui {

void build();
void open(const String& key);
void close();
bool isOpen();
void update();  // iz ui::update(): šalje nameštenu temperaturu, zatvara se posle 30 s bez dodira

}  // namespace thermo_ui
