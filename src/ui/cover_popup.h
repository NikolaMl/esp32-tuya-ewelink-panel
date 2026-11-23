#pragma once
#include <Arduino.h>
#include <lvgl.h>

#include "model/cover.h"

// Prozorče za roletnu (preko bilo kog ekrana): naziv, položaj i dugmad Gore / Stop / Dole.
// Otvara se dodirom na roletnu na početnom ekranu ili na pločicu u listi.
namespace cover_ui {

void build();
void open(const String& key);
void close();
bool isOpen();
void update();  // iz ui::update(): stanje roletne, samo se zatvara posle 30 s bez dodira
// Pošalji komandu (poruka ako je roletna nedostupna); koriste je i pločice u listi
void command(const String& key, CoverAction a);

}  // namespace cover_ui
