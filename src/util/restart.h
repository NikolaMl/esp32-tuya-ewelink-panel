#pragma once
#include <stdint.h>

// Odložen restart panela (npr. posle promene jezika): da web odgovor stigne do telefona
// i da se na ekranu vidi poruka. Bezbedno iz bilo kog taska; restart radi loop().
namespace restart {

void soon(uint32_t delayMs = 1500);
bool pending();
void poll();  // iz loop()

// Da li je ovo pokretanje posle restart::soon() (oznaka u RTC memoriji, briše se pri prvom pozivu).
// Tada se zadržava web PIN, da telefon ostane prijavljen.
bool wasRequested();

}  // namespace restart
