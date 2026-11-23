#include "ui/alarm_screen.h"

#include <vector>

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "ui/settings_screen.h"
#include "ui/theme.h"

namespace alarm_ui {

using namespace theme;

static constexpr int TOP_H = 60;
static constexpr size_t PIN_MIN = 4, PIN_MAX = 8;
static constexpr uint32_t AUTO_CLOSE_MS = 60000;  // bez dodira -> nazad na glavni ekran

static lv_obj_t *s_main, *s_scr, *s_title;
static lv_obj_t *s_icon, *s_state, *s_detail, *s_arc, *s_arcNum;
static lv_obj_t *s_armBox, *s_armMsg, *s_pinBox, *s_pinDots, *s_pinMsg, *s_pad, *s_noPinBox;

static String s_id;     // centrala prikazana na ekranu
static String s_pin;    // uneti PIN (samo u RAM-u, briše se posle pokušaja)
static bool s_showedLock = false;
static String s_acked;  // "id," centrala čiji je alarm korisnik već video (ne otvaraj ponovo)
static uint32_t s_seenRev = 0, s_lastTick = 0;
static bool s_awake = false;
static uint16_t s_exitTotal = 0;

// ---------------------------------------------------------------- izmene bez nepotrebnog prekrtavanja

static void setText(lv_obj_t* l, const String& t) {
  if (strcmp(lv_label_get_text(l), t.c_str()) != 0) lv_label_set_text(l, t.c_str());
}
static void setVisible(lv_obj_t* o, bool v) {
  if (v == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void setTextColor(lv_obj_t* o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}
static void setBg(lv_obj_t* o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) lv_obj_set_style_bg_color(o, c, 0);
}

// ---------------------------------------------------------------- izgled stanja

uint32_t exitSecondsLeft(const AlarmState& a) {
  if (!a.exitUntil) return 0;
  const int32_t ms = (int32_t)(a.exitUntil - millis());
  return ms > 0 ? (ms + 999) / 1000 : 0;
}

Look lookFor(const AlarmState& a) {
  Look l;
  const lv_color_t white = lv_color_hex(0xFFFFFF);
  if (!a.online) {
    l = {C_CARD_OFFLINE, C_MUTED, C_MUTED, "Nedostupan", "Centrala nije na mreži"};
    return l;
  }
  if (a.alarming) {
    l = {lv_color_hex(0xB3261E), white, white, a.mode == AlarmMode::Sos ? "SOS!" : "ALARM!", a.lastMsg};
    return l;
  }
  switch (a.mode) {
    case AlarmMode::Disarmed: l = {C_CARD, C_TEXT, C_OK, "Isključen", ""}; break;
    case AlarmMode::Away: l = {lv_color_hex(0x5C1A1F), white, lv_color_hex(0xFF8A80), "Uključen – odsutan", ""}; break;
    case AlarmMode::Home: l = {lv_color_hex(0x173A5E), white, lv_color_hex(0x8EC5FF), "Uključen – kod kuće", ""}; break;
    default: l = {C_CARD, C_TEXT, C_MUTED, "Nepoznato stanje", ""}; break;
  }
  const uint32_t left = exitSecondsLeft(a);
  if (a.pending) l.detail = "Šaljem komandu…";
  else if (left) l.detail = "Izlazno kašnjenje: " + String(left) + " s";
  else if (a.mode == AlarmMode::Disarmed)
    l.detail = "Izlazno kašnjenje " + String(a.exitDelay) + " s · ulazno " + String(a.entryDelay) + " s";
  return l;
}

// ---------------------------------------------------------------- PIN

static bool locked() { return settings::alarmPinLockedMs() > 0; }

static void showPin() {
  if (s_pin.isEmpty()) {
    setText(s_pinDots, "Unesi PIN");
    setTextColor(s_pinDots, C_MUTED);
    return;
  }
  String dots;
  for (size_t i = 0; i < s_pin.length(); i++) dots += i ? " •" : "•";
  setText(s_pinDots, dots);
  setTextColor(s_pinDots, C_TEXT);
}

static void submitPin() {
  if (locked()) return;
  if (s_pin.length() < PIN_MIN) {
    setText(s_pinMsg, "PIN ima najmanje 4 cifre");
    return;
  }
  const settings::PinResult r = settings::verifyAlarmPin(s_pin);
  s_pin = "";
  showPin();
  if (r != settings::PinResult::Ok) {
    if (r == settings::PinResult::Wrong) setText(s_pinMsg, "Pogrešan PIN");
    return;
  }
  if (cloud::requestAlarm(s_id, AlarmMode::Disarmed)) {
    setText(s_pinMsg, "Isključujem…");
    if (s_acked.indexOf(s_id + ",") < 0) s_acked += s_id + ",";
  } else {
    setText(s_pinMsg, "Centrala je zauzeta ili nedostupna");
  }
}

static void onPad(lv_event_t* e) {
  lv_obj_t* pad = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const uint32_t id = lv_buttonmatrix_get_selected_button(pad);
  const char* txt = lv_buttonmatrix_get_button_text(pad, id);
  if (!txt) return;
  if (locked()) return;
  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
    if (s_pin.length()) s_pin.remove(s_pin.length() - 1);
  } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    submitPin();
    return;
  } else if (s_pin.length() < PIN_MAX) {
    s_pin += txt;
  }
  setText(s_pinMsg, "");
  showPin();
}

// ---------------------------------------------------------------- uključivanje

static void arm(AlarmMode mode) {
  if (cloud::requestAlarm(s_id, mode)) setText(s_armMsg, "");
  else setText(s_armMsg, "Centrala je zauzeta ili nedostupna");
}

// ---------------------------------------------------------------- izgradnja

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* column(lv_obj_t* parent, int32_t x, int32_t w) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, TOP_H + 10);
  lv_obj_set_size(c, w, LCD_V_RES - TOP_H - 20);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 12, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t* bigButton(lv_obj_t* parent, const char* text, lv_color_t bg, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, LV_PCT(100), 96);
  lv_obj_set_style_radius(b, 20, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_border_color(b, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 3, LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = label(b, text, F_NAME, lv_color_hex(0xFFFFFF));
  lv_obj_center(l);
  return b;
}

void build(lv_obj_t* mainScreen) {
  s_main = mainScreen;
  s_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr, C_BG, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  // Gornja traka
  lv_obj_t* back = lv_button_create(s_scr);
  lv_obj_set_size(back, 120, 44);
  lv_obj_set_pos(back, 12, 8);
  lv_obj_set_style_radius(back, 22, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_style_bg_color(back, C_CARD, 0);
  lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(back, 6, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
  label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_TEXT);
  label(back, "Nazad", F_BODY, C_TEXT);
  s_title = label(s_scr, "", F_NAME, C_TEXT);
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 16);

  // Levo: stanje + odbrojavanje
  lv_obj_t* left = column(s_scr, 20, 370);
  s_icon = label(left, LV_SYMBOL_BELL, &lv_font_montserrat_40, C_TEXT);
  s_state = label(left, "", F_TITLE, C_TEXT);
  s_arc = lv_arc_create(left);
  lv_obj_set_size(s_arc, 170, 170);
  lv_arc_set_rotation(s_arc, 270);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, 0, 300);
  lv_obj_remove_style(s_arc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, C_CARD, LV_PART_MAIN);
  s_arcNum = label(s_arc, "", &lv_font_montserrat_40, C_TEXT);
  lv_obj_center(s_arcNum);
  s_detail = label(left, "", F_BODY, C_MUTED);
  lv_obj_set_width(s_detail, 360);
  lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);

  // Desno: dugmad za uključivanje
  s_armBox = column(s_scr, 410, 370);
  bigButton(s_armBox, "Uključi – odsutan", lv_color_hex(0x7A2228), [](lv_event_t*) { arm(AlarmMode::Away); });
  bigButton(s_armBox, "Uključi – kod kuće", lv_color_hex(0x1F4A78), [](lv_event_t*) { arm(AlarmMode::Home); });
  s_armMsg = label(s_armBox, "", F_SMALL, C_ERR);

  // Desno: PIN tastatura za isključivanje
  s_pinBox = column(s_scr, 410, 370);
  lv_obj_set_style_pad_row(s_pinBox, 6, 0);
  s_pinDots = label(s_pinBox, "", F_TITLE, C_MUTED);
  s_pinMsg = label(s_pinBox, "", F_SMALL, lv_color_hex(0xFFB4AB));
  s_pad = lv_buttonmatrix_create(s_pinBox);
  static const char* map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                              LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
  lv_buttonmatrix_set_map(s_pad, map);
  lv_buttonmatrix_set_button_ctrl_all(s_pad, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
  lv_buttonmatrix_set_button_ctrl(s_pad, 11, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(s_pad, 340, 300);
  lv_obj_set_style_text_font(s_pad, &lv_font_montserrat_28, 0);
  lv_obj_set_style_bg_opa(s_pad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_pad, 0, 0);
  lv_obj_set_style_pad_all(s_pad, 0, 0);
  lv_obj_set_style_pad_row(s_pad, 10, 0);
  lv_obj_set_style_pad_column(s_pad, 10, 0);
  lv_obj_set_style_bg_color(s_pad, C_CARD, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_pad, lv_color_hex(0x2F3843), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(s_pad, C_ON, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(s_pad, C_TEXT, LV_PART_ITEMS);
  lv_obj_set_style_text_color(s_pad, C_ON_TEXT, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(s_pad, 16, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(s_pad, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_pad, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(s_pad, onPad, LV_EVENT_VALUE_CHANGED, nullptr);

  // Desno: PIN još nije podešen
  s_noPinBox = column(s_scr, 410, 370);
  lv_obj_t* np = label(s_noPinBox, "Za isključivanje alarma sa panela prvo podesi PIN.", F_BODY, C_TEXT);
  lv_obj_set_width(np, 340);
  lv_label_set_long_mode(np, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(np, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* pb = lv_button_create(s_noPinBox);
  lv_obj_set_height(pb, 56);
  lv_obj_set_style_radius(pb, 28, 0);
  lv_obj_set_style_shadow_width(pb, 0, 0);
  lv_obj_set_style_bg_color(pb, C_ON, 0);
  lv_obj_set_style_pad_hor(pb, 26, 0);
  lv_obj_add_event_cb(pb, [](lv_event_t*) { settings_ui::open(settings_ui::TAB_ALARM); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(pb, "Podesi PIN", F_BODY, C_ON_TEXT));
}

// ---------------------------------------------------------------- osvežavanje

static void refresh(const AlarmState& a) {
  const Look l = lookFor(a);
  setText(s_title, a.name);
  setBg(s_scr, a.alarming ? lv_color_hex(0x3A0D10) : C_BG);
  setText(s_state, l.state);
  setTextColor(s_state, a.alarming ? lv_color_hex(0xFFFFFF) : l.accent);
  setTextColor(s_icon, a.alarming ? lv_color_hex(0xFFFFFF) : l.accent);
  setText(s_detail, l.detail);

  const uint32_t left = exitSecondsLeft(a);
  if (left && !s_exitTotal) s_exitTotal = a.exitDelay ? a.exitDelay : left;
  if (!left) s_exitTotal = 0;
  setVisible(s_arc, left > 0);
  setVisible(s_icon, left == 0);
  if (left) {
    if (lv_arc_get_max_value(s_arc) != s_exitTotal) lv_arc_set_range(s_arc, 0, s_exitTotal);
    if (lv_arc_get_value(s_arc) != (int32_t)left) lv_arc_set_value(s_arc, left);
    if (!lv_color_eq(lv_obj_get_style_arc_color(s_arc, LV_PART_INDICATOR), l.accent))
      lv_obj_set_style_arc_color(s_arc, l.accent, LV_PART_INDICATOR);
    setText(s_arcNum, String(left));
  }

  const bool disarmed = a.mode == AlarmMode::Disarmed && !a.alarming;
  setVisible(s_armBox, a.online && disarmed);
  setVisible(s_pinBox, a.online && !disarmed && settings::hasAlarmPin());
  setVisible(s_noPinBox, a.online && !disarmed && !settings::hasAlarmPin());

  if (locked()) {
    const uint32_t s = (settings::alarmPinLockedMs() + 999) / 1000;
    setText(s_pinMsg, "Previše pogrešnih pokušaja. Sačekaj " + String(s) + " s.");
    s_showedLock = true;
  } else if (s_showedLock) {
    s_showedLock = false;
    setText(s_pinMsg, "");
  }
}

void open(const String& deviceId) {
  s_id = deviceId;
  s_pin = "";
  s_exitTotal = 0;
  showPin();
  setText(s_pinMsg, "");
  setText(s_armMsg, "");
  for (const AlarmState& a : g_store.alarms()) {
    if (a.deviceId == deviceId) refresh(a);
  }
  lv_screen_load(s_scr);
}

void close() {
  if (s_acked.indexOf(s_id + ",") < 0) {
    AlarmInfo a;
    if (g_store.findAlarm(s_id, a) && a.alarming) s_acked += s_id + ",";  // ne otvaraj ponovo isti alarm
  }
  s_pin = "";
  lv_screen_load(s_main);
}

bool isOpen() { return s_scr && lv_screen_active() == s_scr; }

bool wantsAwake() { return s_awake; }

void update() {
  const uint32_t rev = g_store.revision();
  const bool tick = millis() - s_lastTick >= 250;
  if (rev == s_seenRev && !tick) return;
  s_seenRev = rev;
  if (tick) s_lastTick = millis();

  const std::vector<AlarmState> list = g_store.alarms();
  bool awake = false;
  for (const AlarmState& a : list) {
    if (settings::isHidden(a.key())) continue;
    const String tag = a.deviceId + ",";
    const bool acked = s_acked.indexOf(tag) >= 0;
    if (a.alarming) {
      awake = true;
      if (!acked && !(isOpen() && s_id == a.deviceId)) open(a.deviceId);  // alarm: odmah na ekran
    } else if (acked) {
      s_acked.replace(tag, "");  // sledeći alarm opet otvara ekran
    }
    if (exitSecondsLeft(a)) awake = true;
  }
  s_awake = awake;

  if (!isOpen()) return;
  for (const AlarmState& a : list) {
    if (a.deviceId != s_id) continue;
    refresh(a);
    const bool busy = a.alarming || exitSecondsLeft(a) || a.pending;
    if (!busy && lv_display_get_inactive_time(nullptr) > AUTO_CLOSE_MS) close();
    return;
  }
  close();  // centrala je nestala (promenjen Tuya nalog)
}

}  // namespace alarm_ui
