#include "ui/smoke_alert.h"

#include <vector>

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "ui/theme.h"

namespace smoke_alert {

using namespace theme;

static lv_obj_t *s_scr, *s_names;
static String s_acked;  // "id," detektora čiji je alarm korisnik već video
static bool s_awake = false;
static uint32_t s_seenRev = 0, s_seenHidden = 0;

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, text);
  return l;
}

static void acknowledge() {
  for (const SmokeInfo& s : g_store.smokes())
    if (s.alarm && !settings::isHidden(s.key()) && s_acked.indexOf(s.deviceId + ",") < 0) s_acked += s.deviceId + ",";
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  s_awake = false;  // posle potvrde ekran se opet normalno gasi
}

void build() {
  s_scr = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_scr);
  lv_obj_set_size(s_scr, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(0xB3261E), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);  // dodiri ne prolaze do ekrana ispod
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_scr, 14, 0);

  lv_obj_t* icon = lv_label_create(s_scr);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_label_set_text(icon, LV_SYMBOL_WARNING);
  label(s_scr, F_HUGE, "DIM");
  s_names = label(s_scr, F_TITLE, "");
  lv_obj_set_width(s_names, LCD_H_RES - 80);
  lv_label_set_long_mode(s_names, LV_LABEL_LONG_WRAP);
  lv_obj_t* hint = label(s_scr, F_BODY, "Detektor je javio dim. Proveri kuću.\nPanel stanje osvežava sa zakašnjenjem (do 5 min).");
  lv_obj_set_style_text_color(hint, lv_color_hex(0xFFD9D6), 0);

  lv_obj_t* ok = lv_obj_create(s_scr);
  lv_obj_remove_style_all(ok);
  lv_obj_set_size(ok, 260, 68);
  lv_obj_set_style_margin_top(ok, 10, 0);
  lv_obj_set_style_radius(ok, 34, 0);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ok, lv_color_white(), 0);
  lv_obj_set_style_bg_color(ok, lv_color_hex(0xFFD9D6), LV_STATE_PRESSED);
  lv_obj_add_flag(ok, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ok, [](lv_event_t*) { acknowledge(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* t = label(ok, F_NAME, "Video sam");
  lv_obj_set_style_text_color(t, lv_color_hex(0xB3261E), 0);
  lv_obj_center(t);

  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

void update() {
  const uint32_t rev = g_store.revision(), hid = settings::hiddenRevision();
  if (rev == s_seenRev && hid == s_seenHidden) return;
  s_seenRev = rev;
  s_seenHidden = hid;
  String names, fresh;
  bool any = false;
  for (const SmokeInfo& s : g_store.smokes()) {
    const String tag = s.deviceId + ",";
    // Detektor isključen u aplikaciji (Uređaji) panel potpuno ignoriše: bez upozorenja i buđenja ekrana
    if (!s.alarm || settings::isHidden(s.key())) {
      s_acked.replace(tag, "");  // sledeći alarm istog detektora opet otvara upozorenje
      continue;
    }
    any = true;
    if (names.length()) names += '\n';
    names += s.name;
    if (s_acked.indexOf(tag) < 0) fresh += tag;
  }
  s_awake = fresh.length() > 0;  // budan dok upozorenje nije potvrđeno
  if (!any) {
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (strcmp(lv_label_get_text(s_names), names.c_str()) != 0) lv_label_set_text(s_names, names.c_str());
  if (fresh.length()) {  // novi alarm: preko svega
    // Samo ako već nije otvoreno i na vrhu: oba poziva ponovo iscrtavaju CEO ekran (sa fotografijom
    // ispod, ~250 ms), a update() se zove pri svakom osvežavanju (10 s) -> ekran je treperio.
    if (lv_obj_has_flag(s_scr, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_get_index(s_scr) != (int32_t)lv_obj_get_child_count(lv_layer_top()) - 1) lv_obj_move_foreground(s_scr);
  }
}

bool wantsAwake() { return s_awake; }
bool isOpen() { return s_scr && !lv_obj_has_flag(s_scr, LV_OBJ_FLAG_HIDDEN); }

}  // namespace smoke_alert
