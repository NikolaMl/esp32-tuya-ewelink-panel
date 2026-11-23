#pragma once
#include <Arduino.h>
#include <lvgl.h>

#include <functional>

// Ekran za unos PIN-a alarma pre zaštićene radnje (ulazak u podešavanja).
// Koristi isto brojanje pogrešnih pokušaja i zaključavanje kao isključivanje alarma.
namespace pin_prompt {

void build();
void ask(const String& title, std::function<void()> onOk);  // Otkaži -> nazad na prethodni ekran
bool isOpen();
void update();  // odbrojavanje zaključavanja

}  // namespace pin_prompt
