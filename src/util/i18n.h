#pragma once
#include <stdint.h>

// Jezik interfejsa (panel i web aplikacija): srpski (latinica) ili engleski.
// Ekrani se prave jednom pri pokretanju, pa se promena jezika primenjuje restartom panela
// (settings::setLanguage + restart::soon). Do restarta važi jezik sa kojim je panel pokrenut.
//
// Tekst se piše na oba jezika na mestu gde se koristi:  TR("Podešavanja", "Settings")
// Oba argumenta su string literali (traju zauvek), pa je vraćeni pokazivač uvek ispravan.
enum class Lang : uint8_t { En = 0, Sr = 1 };

namespace i18n {

Lang lang();         // jezik ovog pokretanja
void set(Lang l);    // samo settings::begin (pri pokretanju)
inline bool en() { return lang() == Lang::En; }
inline const char* tr(const char* sr, const char* en_) { return en() ? en_ : sr; }
inline char decimalSep() { return en() ? '.' : ','; }  // 21,5° (sr) / 21.5° (en)

const char* code(Lang l);                  // "en" / "sr"
bool parse(const char* code, Lang& out);   // "en" / "sr" -> Lang (false = nepoznat)

}  // namespace i18n

#define TR(sr, en) ::i18n::tr(sr, en)
