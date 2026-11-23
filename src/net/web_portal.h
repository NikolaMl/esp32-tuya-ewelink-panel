#pragma once
#include <Arduino.h>

// Mali web server na panelu (port 80):
//   /                     -> podešavanje Tuya / eWeLink ključeva (zaštićeno PIN-om sa ekrana)
//   /ewelink/login?s=..   -> preusmerenje na eWeLink prijavu (link iz QR koda na ekranu)
//   /ewelink/callback     -> eWeLink vraća "code", panel ga menja za tokene
namespace portal {

void begin();

String baseUrl();       // http://<ip>/
String redirectUrl();   // http://<ip>/ewelink/callback  (ovo se upisuje u dev.ewelink.cc)
String newLoginUrl();   // novi jednokratni link za QR kod (važi 10 min)

// Rezultat poslednje eWeLink prijave (za prikaz na ekranu)
uint32_t oauthSeq();
bool oauthOk();
String oauthMessage();

}  // namespace portal
