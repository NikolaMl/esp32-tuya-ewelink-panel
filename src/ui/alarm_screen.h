#pragma once
#include <Arduino.h>
#include <lvgl.h>

#include "model/alarm.h"

// Ekran alarm centrale: stanje, uključivanje (odsutan / kod kuće), isključivanje PIN-om,
// odbrojavanje izlaznog kašnjenja. Kad centrala javi alarm, ekran se sam otvori.
namespace alarm_ui {

void build(lv_obj_t* mainScreen);
void open(const String& deviceId);
void close();
bool isOpen();
void update();      // iz loop-a: odbrojavanje, automatsko otvaranje pri alarmu
bool wantsAwake();  // alarm ili izlazno kašnjenje u toku -> ekran ne gasiti

// Zajedničko za pločicu na glavnom ekranu i ovaj ekran
struct Look {
  lv_color_t bg, fg, accent;
  String state;   // "Isključen", "Uključen – odsutan", "ALARM!"…
  String detail;  // odbrojavanje, poslednja poruka…
};
Look lookFor(const AlarmState& a);
uint32_t exitSecondsLeft(const AlarmState& a);

}  // namespace alarm_ui
