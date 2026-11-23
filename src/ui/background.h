#pragma once
#include <lvgl.h>

#include <functional>

// Fotografije delova početnog ekrana (800x480 RGB565). Deo 0: /bg.rgb565 u LittleFS-u (sa telefona),
// a ako ga nema, ugrađena slika (assets/background.rgb565). Deo N>0: /bgN.rgb565; bez fajla deo nema sliku.
// Prikazana slika je u PSRAM-u; novi deo se učitava u drugi bafer pa se tek onda zameni.
namespace background {

constexpr size_t SIZE = 800 * 480 * 2;

// ---- UI task
const lv_image_dsc_t* load(uint8_t page);  // nullptr = deo nema sliku (ili greška)

// ---- web task
const char* const TMP_PATH = "/bg.tmp";  // slanje ide ovde, pa se zameni kad je cela slika stigla
bool fsReady();
bool hasImage(uint8_t page);                                  // fajl ili (deo 0) ugrađena slika
bool hasCustom(uint8_t page);                                 // fajl sa telefona
bool stream(uint8_t page, const std::function<void(const uint8_t*, size_t)>& sink);  // pikseli za telefon
bool removeCustom(uint8_t page);                              // deo 0 -> ugrađena slika, ostali -> bez slike
bool commitUpload(uint8_t page);                              // TMP_PATH -> fajl tog dela
bool deletePage(uint8_t page, uint8_t count);                 // obriši fajl, kasnije delove pomeri za jedan
// Demo prikaz (konzola "show"): deo 0 prikazuje ugrađenu sliku i kad postoji fotografija sa telefona.
// Samo u RAM-u; fajl ostaje, a posle restarta je sve kao pre.
void setBuiltinOnly(bool on);
void markChanged();                                           // web task: slika je promenjena
bool consumeChanged();                                        // UI task: da li ponovo učitati

}  // namespace background
