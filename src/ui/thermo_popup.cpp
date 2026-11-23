#include "ui/thermo_popup.h"

#include <math.h>

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "ui/theme.h"
#include "ui/ui.h"

namespace thermo_ui {

using namespace theme;

static constexpr int CARD_W = 580, CARD_H = 340, ROUND_D = 104;
static constexpr uint32_t AUTO_CLOSE_MS = 30000, SEND_AFTER_MS = 1200;

static lv_obj_t *s_scrim, *s_name, *s_state, *s_target, *s_power, *s_powerText, *s_hint;
static lv_obj_t* s_openedOn = nullptr;  // ekran ispod; ako se promeni, prozorče se zatvara
static String s_key;
static uint32_t s_seenRev = 0;
static float s_temp = NAN;      // prikazana željena temperatura
static bool s_dirty = false;    // promenjena dodirom, još nije poslata
static uint32_t s_lastTap = 0;

static void setText(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static bool current(ThermostatState& out) {
  for (const ThermostatState& t : g_store.thermostats()) {
    if (t.key() != s_key) continue;
    out = t;
    return true;
  }
  return false;
}

static void sendPending() {
  if (!s_dirty) return;
  s_dirty = false;
  if (!cloud::requestThermostat(s_key, -1, s_temp)) ui::showToast("Komanda nije poslata, pokušaj ponovo");
}

static void refresh() {
  ThermostatState t;
  if (settings::isHidden(s_key)) {  // isključen u aplikaciji: zatvori bez slanja
    s_dirty = false;
    close();
    return;
  }
  if (!current(t)) {
    close();  // termostat je nestao sa naloga
    return;
  }
  setText(s_name, t.name.c_str());
  String st;
  if (!t.online) {
    st = "Nedostupan";
  } else {
    st = "Trenutno " + (t.hasCurrent ? thermoDegrees(t.current) : String("--"));
    if (!t.on) st += " · isključen";
    else if (t.heating == 1) st += " · greje";
    else if (t.heating == 0) st += " · ne greje";
    if (t.mode.length()) st += " · " + thermoModeText(t.mode);
    if (t.pending) st += " · šaljem…";
  }
  setText(s_state, st.c_str());
  if (!s_dirty && t.hasTarget) s_temp = t.target;  // dok se dodiruje, važi prikazana vrednost
  setText(s_target, t.hasTarget || !isnan(s_temp) ? thermoDegrees(s_temp).c_str() : "--");
  const lv_color_t tc = t.online && t.on ? C_TEXT : C_MUTED;
  if (!lv_color_eq(lv_obj_get_style_text_color(s_target, LV_PART_MAIN), tc)) lv_obj_set_style_text_color(s_target, tc, 0);
  if (t.hasPower == lv_obj_has_flag(s_power, LV_OBJ_FLAG_HIDDEN)) {  // samo kad se menja (inače iscrtava ponovo)
    if (t.hasPower) lv_obj_remove_flag(s_power, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_power, LV_OBJ_FLAG_HIDDEN);
  }
  setText(s_powerText, t.on ? "Isključi" : "Uključi");
  const bool sched = t.mode == "auto" || t.mode == "program" || t.mode == "schedule" || t.mode == "smart";
  setText(s_hint, sched ? "Po rasporedu: termostat može sam da vrati temperaturu iz svog rasporeda." : "");
}

static void onStep(lv_event_t* e) {
  const int dir = (int)(intptr_t)lv_event_get_user_data(e);
  ThermostatState t;
  if (!current(t)) return;
  if (!t.online) {
    ui::showToast(t.name + " je trenutno nedostupan");
    return;
  }
  if (!t.hasTarget && isnan(s_temp)) {
    ui::showToast(t.name + " ne javlja željenu temperaturu");
    return;
  }
  s_temp = thermoClamp(t, (isnan(s_temp) ? t.target : s_temp) + dir * t.step);
  s_dirty = true;
  s_lastTap = millis();
  setText(s_target, thermoDegrees(s_temp).c_str());
}

static void onPower(lv_event_t*) {
  ThermostatState t;
  if (!current(t)) return;
  if (!t.online) {
    ui::showToast(t.name + " je trenutno nedostupan");
    return;
  }
  sendPending();
  if (!cloud::requestThermostat(s_key, t.on ? 0 : 1, NAN)) ui::showToast("Komanda nije poslata, pokušaj ponovo");
}

static lv_obj_t* roundButton(lv_obj_t* parent, const char* symbol, int dir) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, ROUND_D, ROUND_D);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x262E38), 0);
  lv_obj_set_style_bg_color(b, C_ON, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, C_TEXT, 0);
  lv_obj_set_style_text_color(b, C_ON_TEXT, LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(b, onStep, LV_EVENT_CLICKED, (void*)(intptr_t)dir);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_40, 0);
  lv_label_set_text(l, symbol);
  lv_obj_center(l);
  return b;
}

void build() {
  s_scrim = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_scrim);
  lv_obj_set_size(s_scrim, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_scrim, 150, 0);
  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);  // dodir pored prozorčeta ga zatvara
  lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_scrim, [](lv_event_t* e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) close();
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* card = lv_obj_create(s_scrim);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_center(card);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, C_CARD, 0);
  lv_obj_set_style_border_color(card, lv_color_white(), 0);
  lv_obj_set_style_border_opa(card, 30, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 24, 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);  // dodir na karticu ne zatvara
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  s_name = label(card, F_NAME, C_TEXT);
  lv_obj_set_width(s_name, CARD_W - 48 - 60);
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
  lv_obj_align(s_name, LV_ALIGN_TOP_LEFT, 0, 0);
  s_state = label(card, F_SMALL, C_MUTED);
  lv_obj_set_width(s_state, CARD_W - 48 - 60);
  lv_label_set_long_mode(s_state, LV_LABEL_LONG_DOT);
  lv_obj_align(s_state, LV_ALIGN_TOP_LEFT, 0, 34);

  lv_obj_t* x = lv_obj_create(card);
  lv_obj_remove_style_all(x);
  lv_obj_set_size(x, 48, 48);
  lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(x, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(x, lv_color_hex(0x262E38), 0);
  lv_obj_set_style_bg_color(x, lv_color_hex(0x3A4450), LV_STATE_PRESSED);
  lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(x, 10);
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 6, -6);
  lv_obj_add_event_cb(x, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(x, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_CLOSE));

  // − 22,5° +
  lv_obj_t* minus = roundButton(card, LV_SYMBOL_MINUS, -1);
  lv_obj_align(minus, LV_ALIGN_LEFT_MID, 10, 6);
  lv_obj_t* plus = roundButton(card, LV_SYMBOL_PLUS, 1);
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -10, 6);
  s_target = label(card, F_HUGE, C_TEXT, "--");
  lv_obj_align(s_target, LV_ALIGN_CENTER, 0, -4);
  lv_obj_t* cap = label(card, F_SMALL, C_MUTED, "željena temperatura");
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, 40);

  // Uključi / Isključi (samo ako uređaj ima prekidač) i napomena za režim po rasporedu
  s_power = lv_obj_create(card);
  lv_obj_remove_style_all(s_power);
  lv_obj_set_size(s_power, 180, 52);
  lv_obj_align(s_power, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_radius(s_power, 26, 0);
  lv_obj_set_style_bg_opa(s_power, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_power, lv_color_hex(0x262E38), 0);
  lv_obj_set_style_bg_color(s_power, C_ON, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(s_power, C_TEXT, 0);
  lv_obj_set_style_text_color(s_power, C_ON_TEXT, LV_STATE_PRESSED);
  lv_obj_add_flag(s_power, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_power, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s_power, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_power, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_power, 8, 0);
  lv_obj_add_event_cb(s_power, onPower, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pi = lv_label_create(s_power);
  lv_obj_set_style_text_font(pi, &lv_font_montserrat_20, 0);
  lv_label_set_text(pi, LV_SYMBOL_POWER);
  s_powerText = lv_label_create(s_power);
  lv_obj_set_style_text_font(s_powerText, F_BODY, 0);
  lv_label_set_text(s_powerText, "Isključi");

  s_hint = label(card, F_SMALL, C_MUTED);
  lv_obj_set_width(s_hint, CARD_W - 48 - 200);
  lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
}

void open(const String& key) {
  sendPending();  // drugi termostat: prvo pošalji nameštenu vrednost prethodnog
  s_key = key;
  s_dirty = false;
  s_temp = NAN;
  s_openedOn = lv_screen_active();
  s_seenRev = g_store.revision();
  lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_scrim);
  refresh();
  cloud::requestRefresh();  // sveže stanje odmah
}

void close() {
  sendPending();  // zatvaranje odmah posle dodira ne gubi nameštenu temperaturu
  if (s_scrim) lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  s_key = "";
}

bool isOpen() { return s_scrim && !lv_obj_has_flag(s_scrim, LV_OBJ_FLAG_HIDDEN); }

void update() {
  if (!isOpen()) return;
  if (settings::isHidden(s_key)) {  // isključen u aplikaciji dok je prozorče otvoreno: bez slanja
    s_dirty = false;
    close();
    return;
  }
  if (s_dirty && millis() - s_lastTap >= SEND_AFTER_MS) sendPending();
  if (lv_screen_active() != s_openedOn || lv_display_get_inactive_time(nullptr) > AUTO_CLOSE_MS) {
    close();
    return;
  }
  const uint32_t rev = g_store.revision();
  if (rev != s_seenRev) {
    s_seenRev = rev;
    refresh();
  }
}

}  // namespace thermo_ui
