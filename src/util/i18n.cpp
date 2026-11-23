#include "util/i18n.h"

#include <string.h>

#include <atomic>

namespace i18n {

static std::atomic<Lang> s_lang{Lang::En};

Lang lang() { return s_lang.load(std::memory_order_relaxed); }
void set(Lang l) { s_lang.store(l, std::memory_order_relaxed); }

const char* code(Lang l) { return l == Lang::Sr ? "sr" : "en"; }

bool parse(const char* c, Lang& out) {
  if (!c) return false;
  if (!strcmp(c, "sr")) {
    out = Lang::Sr;
    return true;
  }
  if (!strcmp(c, "en")) {
    out = Lang::En;
    return true;
  }
  return false;
}

}  // namespace i18n
