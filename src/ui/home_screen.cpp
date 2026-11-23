#include "ui/home_screen.h"

#include <Arduino.h>
#include <time.h>

#include <vector>

#include "config.h"
#include "model/device_store.h"
#include "model/scenes.h"
#include "model/settings.h"
#include "ui/alarm_screen.h"
#include "ui/background.h"
#include "ui/cover_popup.h"
#include "ui/settings_screen.h"
#include "ui/thermo_popup.h"
#include "ui/theme.h"
#include "ui/ui.h"

namespace home_ui {

using namespace theme;

static constexpr int LIGHT_D = 64;  // prečnik dugmeta svetla

static lv_obj_t *s_scr, *s_layer, *s_bg;
static lv_obj_t *s_clock, *s_date, *s_wifi;
static lv_obj_t *s_lock, *s_shackle, *s_body, *s_lockText;
static lv_obj_t *s_status, *s_statusText;
static lv_obj_t* s_editBar;
static lv_obj_t *s_dots, *s_noImg;  // tačkice delova; poruka kad deo nema fotografiju
static lv_obj_t *s_prev, *s_next;   // strelice za delove, dole levo i desno
static uint8_t s_page = 0;          // prikazani deo početnog ekrana
static bool s_edit = false;
static String s_lockAlarm;  // id centrale na koju vodi katanac
static uint32_t s_seenRev = 0, s_seenHidden = 0, s_seenUi = 0, s_lastTick = 0;

enum class Kind : uint8_t { Sensor, Light, Cover, Smoke, Scene, Thermo, Binary };
struct Item {
  Kind kind;
  String key;
  lv_obj_t* root;    // pomera se pri uređivanju
  lv_obj_t* grip;    // deo koji se hvata (pilula senzora / krug svetla)
  lv_obj_t* value;   // temperatura / ikonica
  lv_obj_t* name;
  int8_t shown = -1;
  uint8_t style = settings::STYLE_BUTTON;  // svetlo: dugme ili nevidljivo polje
  uint16_t slot = 0;                       // redni broj u rasporedu (isti uređaj može više puta)
};
static std::vector<Item> s_items;
static std::vector<settings::Position> s_debugPos;  // konzola "dt": probne oznake samo u RAM-u
// Demo prikaz (konzola "show"): samo probne oznake, jedan deo, bez sačuvanog rasporeda (do restarta)
static bool s_show = false;
static uint8_t pageCount() { return s_show ? 1 : settings::pageCount(); }

// ---------------------------------------------------------------- pomoćne

static void setText(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
static void setVisible(lv_obj_t* o, bool v) {
  if (v == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void setTextColor(lv_obj_t* o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}
static void setBgColor(lv_obj_t* o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) lv_obj_set_style_bg_color(o, c, 0);
}
static void setBorderColor(lv_obj_t* o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_border_color(o, LV_PART_MAIN), c)) lv_obj_set_style_border_color(o, c, 0);
}

static const lv_color_t WHITE = lv_color_hex(0xFFFFFF);
static const lv_color_t SOFT = lv_color_hex(0xD5DCE4);

// "Staklo": tamna providna podloga sa tankom svetlom ivicom
static void glass(lv_obj_t* o, lv_opa_t opa = 140) {
  lv_obj_set_style_bg_color(o, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_border_color(o, WHITE, 0);
  lv_obj_set_style_border_opa(o, 45, 0);
  lv_obj_set_style_border_width(o, 1, 0);
}

static lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* roundButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t cb) {
  lv_obj_t* b = box(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, 54, 54);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  glass(b);
  lv_obj_set_style_bg_opa(b, 210, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(b, &lv_font_montserrat_24, WHITE, symbol));
  return b;
}

// ---------------------------------------------------------------- uređivanje rasporeda

static void clampPos(lv_obj_t* root, int32_t& x, int32_t& y) {
  const int32_t w = lv_obj_get_width(root), h = lv_obj_get_height(root);
  x = LV_CLAMP(0, x, LCD_H_RES - w);
  y = LV_CLAMP(0, y, LCD_V_RES - h);
}

static void onGripPressing(lv_event_t* e) {
  if (!s_edit) return;
  lv_obj_t* root = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (!v.x && !v.y) return;
  int32_t x = lv_obj_get_x(root) + v.x, y = lv_obj_get_y(root) + v.y;
  clampPos(root, x, y);
  lv_obj_set_pos(root, x, y);
}

static void onGripReleased(lv_event_t* e) {
  if (!s_edit) return;
  lv_obj_t* root = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
  for (const Item& it : s_items) {
    if (it.root == root)
      settings::movePosition(it.slot, it.key, lv_obj_get_x(root) + lv_obj_get_width(root) / 2,
                             lv_obj_get_y(root) + lv_obj_get_height(root) / 2);
  }
}

static void updateDots();

// Naziv (kod svetla: pločica sa nazivom) se vidi samo dok se raspoređuje
static lv_obj_t* nameBox(const Item& it) {
  return it.kind == Kind::Light || it.kind == Kind::Cover ? lv_obj_get_parent(it.name) : it.name;
}

void setEditMode(bool on) {
  if (on == s_edit) return;
  s_edit = on;
  for (Item& it : s_items) {
    if (it.kind == Kind::Sensor || it.kind == Kind::Smoke || it.kind == Kind::Binary) {
      if (on) lv_obj_add_flag(it.grip, LV_OBJ_FLAG_CLICKABLE);
      else lv_obj_remove_flag(it.grip, LV_OBJ_FLAG_CLICKABLE);
    }
    // Prikaz/skrivanje naziva menja veličinu oznake; centar oznake ostaje na mestu
    lv_obj_update_layout(it.root);
    const int32_t cx = lv_obj_get_x(it.root) + lv_obj_get_width(it.root) / 2;
    const int32_t cy = lv_obj_get_y(it.root) + lv_obj_get_height(it.root) / 2;
    if (it.kind != Kind::Scene) setVisible(nameBox(it), on);
    lv_obj_update_layout(it.root);
    int32_t x = cx - lv_obj_get_width(it.root) / 2, y = cy - lv_obj_get_height(it.root) / 2;
    clampPos(it.root, x, y);
    lv_obj_set_pos(it.root, x, y);
    lv_obj_set_style_outline_width(it.grip, on ? 2 : 0, 0);
  }
  setVisible(s_editBar, on);
  updateDots();  // tačkice i strelice se kriju dok se raspoređuje
  if (!on && !s_show) {  // demo prikaz ne dira sačuvan raspored
    settings::savePositions();
    Serial.println("[ui] raspored sačuvan");
  }
}

// ---------------------------------------------------------------- stavke na slici

static void onLightClicked(lv_event_t* e) {
  if (s_edit) return;
  ui::toggleSwitch(static_cast<const char*>(lv_event_get_user_data(e)));
}

static void onCoverClicked(lv_event_t* e) {
  if (s_edit) return;
  cover_ui::open(static_cast<const char*>(lv_event_get_user_data(e)));
}

static void freeUserData(lv_event_t* e) { free(lv_event_get_user_data(e)); }

// Položaj = centar oznake iz sačuvanog rasporeda
static void placeItem(Item& it, const settings::Position& p) {
  lv_obj_update_layout(it.root);
  int32_t px = p.x - lv_obj_get_width(it.root) / 2, py = p.y - lv_obj_get_height(it.root) / 2;
  clampPos(it.root, px, py);
  lv_obj_set_pos(it.root, px, py);
}

// "Dnevna soba THS" -> "Dnevna soba" (oznaka modela ne treba na slici)
static String shortName(String n) {
  n.trim();
  if (n.endsWith(" THS")) n.remove(n.length() - 4);
  return n;
}

static Item makeSensor(const SensorInfo& s) {
  Item it;
  it.kind = Kind::Sensor;
  it.key = s.key();
  it.root = it.grip = box(s_layer);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(it.root, 18, 0);
  lv_obj_set_style_pad_hor(it.root, 14, 0);
  lv_obj_set_style_pad_ver(it.root, 6, 0);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_outline_color(it.root, C_ON, 0);
  lv_obj_set_style_outline_pad(it.root, 3, 0);
  glass(it.root);
  it.value = label(it.root, F_TITLE, WHITE);
  // Širina po sadržaju: LONG_DOT tada ne radi (prikaže samo "…"), pa duži naziv ide u dva reda
  it.name = label(it.root, F_SMALL, SOFT);  // vidi se samo pri raspoređivanju
  lv_obj_set_style_max_width(it.name, 190, 0);
  if (!s_edit) lv_obj_add_flag(it.name, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(it.root, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.root, onGripReleased, LV_EVENT_RELEASED, it.root);
  if (s_edit) lv_obj_add_flag(it.root, LV_OBJ_FLAG_CLICKABLE);
  return it;
}

static void applySensor(Item& it, const SensorInfo& s) {
  char buf[16] = "--";
  if (s.hasValue) {
    snprintf(buf, sizeof(buf), "%.1f°", s.value);
    if (char* dot = strchr(buf, '.')) *dot = ',';
  }
  setText(it.value, buf);
  const String n = shortName(s.name);
  setText(it.name, s.online ? n.c_str() : (n + " · nedostupan").c_str());
  setTextColor(it.value, s.online ? WHITE : lv_color_hex(0x8A939E));
}

// Okrugla oznaka (svetlo, roletna): dugme sa ikonicom ili nevidljivo polje preko fotografije
static Item makeRound(Kind kind, const String& key, uint8_t style, const lv_font_t* iconFont, const char* icon,
                      lv_event_cb_t onClick) {
  Item it;
  it.kind = kind;
  it.key = key;
  it.style = style;
  it.root = box(s_layer);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(it.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(it.root, 6, 0);
  lv_obj_set_style_pad_all(it.root, 4, 0);  // mesto za sjaj

  it.grip = box(it.root);
  lv_obj_add_flag(it.grip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(it.grip, LIGHT_D, LIGHT_D);
  lv_obj_set_style_radius(it.grip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_outline_color(it.grip, C_ON, 0);
  lv_obj_set_style_outline_pad(it.grip, 3, 0);
  lv_obj_set_style_transform_scale(it.grip, 235, LV_STATE_PRESSED);  // malo "utone" na dodir
  lv_obj_set_style_transform_pivot_x(it.grip, LIGHT_D / 2, 0);
  lv_obj_set_style_transform_pivot_y(it.grip, LIGHT_D / 2, 0);
  lv_obj_set_ext_click_area(it.grip, 8);
  glass(it.grip);
  it.value = label(it.grip, iconFont, WHITE, icon);
  lv_obj_set_style_text_align(it.value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(it.value, -4, 0);
  lv_obj_center(it.value);
  if (style == settings::STYLE_INVISIBLE) {
    // Polje preko sijalice na fotografiji: bez ikonice, na dodir kratko bljesne
    lv_obj_add_flag(it.value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(it.grip, WHITE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(it.grip, 90, LV_STATE_PRESSED);
  }

  lv_obj_t* pill = box(it.root);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(pill, 10, 0);
  lv_obj_set_style_pad_hor(pill, 8, 0);
  lv_obj_set_style_pad_ver(pill, 2, 0);
  glass(pill, 120);
  lv_obj_set_style_border_width(pill, 0, 0);
  it.name = label(pill, F_SMALL, WHITE);
  lv_obj_set_style_max_width(it.name, 150, 0);
  if (!s_edit) lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);  // naziv samo pri raspoređivanju

  char* k = strdup(it.key.c_str());
  lv_obj_add_event_cb(it.grip, onClick, LV_EVENT_CLICKED, k);
  lv_obj_add_event_cb(it.grip, freeUserData, LV_EVENT_DELETE, k);
  lv_obj_add_event_cb(it.grip, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.grip, onGripReleased, LV_EVENT_RELEASED, it.root);
  return it;
}

// Scena: pločica [▶ Naziv]; dodir je pokreće, pri raspoređivanju se prevlači
static void onSceneClicked(lv_event_t* e) {
  if (s_edit) return;
  ui::runScene(static_cast<const char*>(lv_event_get_user_data(e)));
}

static Item makeScene(const scenes::Scene& sc) {
  Item it;
  it.kind = Kind::Scene;
  it.key = scenes::keyFor(sc.id);
  it.root = it.grip = box(s_layer);
  lv_obj_add_flag(it.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, 52);
  lv_obj_set_style_radius(it.root, 26, 0);
  lv_obj_set_style_pad_left(it.root, 14, 0);
  lv_obj_set_style_pad_right(it.root, 18, 0);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(it.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(it.root, 10, 0);
  lv_obj_set_style_outline_color(it.root, C_ON, 0);
  lv_obj_set_style_outline_pad(it.root, 3, 0);
  glass(it.root, 170);
  lv_obj_set_style_bg_color(it.root, C_ON, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(it.root, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(it.root, 6);
  it.value = label(it.root, &lv_font_montserrat_20, C_ON, LV_SYMBOL_PLAY);
  it.name = label(it.root, F_BODY, WHITE, sc.name.c_str());
  lv_obj_set_style_max_width(it.name, 220, 0);
  char* id = strdup(sc.id.c_str());
  lv_obj_add_event_cb(it.root, onSceneClicked, LV_EVENT_CLICKED, id);
  lv_obj_add_event_cb(it.root, freeUserData, LV_EVENT_DELETE, id);
  lv_obj_add_event_cb(it.root, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.root, onGripReleased, LV_EVENT_RELEASED, it.root);
  return it;
}

// Detektor dima: pločica [ikonica Dim]; crvena "DIM!" kad javlja dim. Naziv samo pri raspoređivanju.
static Item makeSmoke(const SmokeInfo& s) {
  Item it;
  it.kind = Kind::Smoke;
  it.key = s.key();
  it.root = it.grip = box(s_layer);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(it.root, 16, 0);
  lv_obj_set_style_pad_hor(it.root, 12, 0);
  lv_obj_set_style_pad_ver(it.root, 6, 0);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(it.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_outline_color(it.root, C_ON, 0);
  lv_obj_set_style_outline_pad(it.root, 3, 0);
  glass(it.root);
  lv_obj_t* row = box(it.root);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  it.value = label(row, &lv_font_montserrat_20, WHITE, LV_SYMBOL_OK);  // ikonica stanja
  lv_obj_t* text = label(row, F_BODY, WHITE, "Dim");
  lv_obj_set_user_data(it.root, text);
  it.name = label(it.root, F_SMALL, SOFT);  // vidi se samo pri raspoređivanju
  lv_obj_set_style_max_width(it.name, 190, 0);
  if (!s_edit) lv_obj_add_flag(it.name, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(it.root, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.root, onGripReleased, LV_EVENT_RELEASED, it.root);
  if (s_edit) lv_obj_add_flag(it.root, LV_OBJ_FLAG_CLICKABLE);
  return it;
}

static void applySmoke(Item& it, const SmokeInfo& s) {
  setText(it.name, s.online ? s.name.c_str() : (s.name + " · nedostupan").c_str());
  const bool warn = s.online && (s.fault || s.tamper || s.batteryLow);
  const int8_t shown = (s.online ? 1 : 0) | (s.alarm ? 2 : 0) | (warn ? 4 : 0);
  if (shown == it.shown) return;
  it.shown = shown;
  lv_obj_t* text = static_cast<lv_obj_t*>(lv_obj_get_user_data(it.root));
  const bool alarm = s.online && s.alarm;
  lv_obj_set_style_bg_color(it.root, alarm ? lv_color_hex(0xB3261E) : lv_color_black(), 0);
  lv_obj_set_style_bg_opa(it.root, alarm ? LV_OPA_COVER : 140, 0);
  setText(it.value, alarm || warn || !s.online ? LV_SYMBOL_WARNING : LV_SYMBOL_OK);
  setTextColor(it.value, alarm ? WHITE : !s.online ? lv_color_hex(0x8A939E) : warn ? C_WARN : C_OK);
  setText(text, alarm ? "DIM!" : "Dim");
  setTextColor(text, s.online ? WHITE : lv_color_hex(0x8A939E));
}

// Termostat: pilula [21,5° / → 22°]; dodir otvara prozorče sa − / +, pri raspoređivanju se prevlači
static void onThermoClicked(lv_event_t* e) {
  if (s_edit) return;
  thermo_ui::open(static_cast<const char*>(lv_event_get_user_data(e)));
}

static Item makeThermo(const ThermostatState& t) {
  Item it;
  it.kind = Kind::Thermo;
  it.key = t.key();
  it.root = it.grip = box(s_layer);
  lv_obj_add_flag(it.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(it.root, 18, 0);
  lv_obj_set_style_pad_hor(it.root, 14, 0);
  lv_obj_set_style_pad_ver(it.root, 6, 0);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(it.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_outline_color(it.root, C_ON, 0);
  lv_obj_set_style_outline_pad(it.root, 3, 0);
  glass(it.root);
  lv_obj_set_style_bg_opa(it.root, 220, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(it.root, 6);
  it.value = label(it.root, F_TITLE, WHITE);  // trenutna temperatura
  lv_obj_t* target = label(it.root, F_SMALL, SOFT);  // "→ 22° · greje" / "isključen"
  lv_obj_set_user_data(it.root, target);
  it.name = label(it.root, F_SMALL, SOFT);  // vidi se samo pri raspoređivanju
  lv_obj_set_style_max_width(it.name, 190, 0);
  if (!s_edit) lv_obj_add_flag(it.name, LV_OBJ_FLAG_HIDDEN);
  char* k = strdup(it.key.c_str());
  lv_obj_add_event_cb(it.root, onThermoClicked, LV_EVENT_CLICKED, k);
  lv_obj_add_event_cb(it.root, freeUserData, LV_EVENT_DELETE, k);
  lv_obj_add_event_cb(it.root, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.root, onGripReleased, LV_EVENT_RELEASED, it.root);
  return it;
}

static void applyThermo(Item& it, const ThermostatState& t) {
  setText(it.name, t.online ? t.name.c_str() : (t.name + " · nedostupan").c_str());
  char cur[16] = "--";
  if (t.hasCurrent) {
    snprintf(cur, sizeof(cur), "%.1f°", t.current);
    if (char* dot = strchr(cur, '.')) *dot = ',';
  }
  setText(it.value, cur);
  setTextColor(it.value, t.online ? WHITE : lv_color_hex(0x8A939E));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_obj_get_user_data(it.root));
  String line;
  if (!t.online) line = "nedostupan";
  else if (!t.on) line = "isključen";
  else if (t.hasTarget) line = "→ " + thermoDegrees(t.target) + (t.heating == 1 ? " · greje" : "");
  setText(target, line.c_str());
  setTextColor(target, t.online && t.on && t.heating == 1 ? C_ON : SOFT);
}

// Senzor vrata / pokreta: pločica [● Otvoreno]; narandžasto kad su vrata otvorena, žuto kad je pokret
static Item makeBinary(const BinarySensorState& b) {
  Item it;
  it.kind = Kind::Binary;
  it.key = b.key();
  it.root = it.grip = box(s_layer);
  lv_obj_set_size(it.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(it.root, 16, 0);
  lv_obj_set_style_pad_hor(it.root, 12, 0);
  lv_obj_set_style_pad_ver(it.root, 6, 0);
  lv_obj_set_flex_flow(it.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(it.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_outline_color(it.root, C_ON, 0);
  lv_obj_set_style_outline_pad(it.root, 3, 0);
  glass(it.root);
  lv_obj_t* row = box(it.root);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  it.value = box(row);  // tačka stanja
  lv_obj_set_size(it.value, 12, 12);
  lv_obj_set_style_radius(it.value, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(it.value, LV_OPA_COVER, 0);
  lv_obj_t* text = label(row, F_BODY, WHITE);
  lv_obj_set_user_data(it.root, text);
  it.name = label(it.root, F_SMALL, SOFT);  // vidi se samo pri raspoređivanju
  lv_obj_set_style_max_width(it.name, 190, 0);
  if (!s_edit) lv_obj_add_flag(it.name, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(it.root, onGripPressing, LV_EVENT_PRESSING, it.root);
  lv_obj_add_event_cb(it.root, onGripReleased, LV_EVENT_RELEASED, it.root);
  if (s_edit) lv_obj_add_flag(it.root, LV_OBJ_FLAG_CLICKABLE);
  return it;
}

static void applyBinary(Item& it, const BinarySensorState& b) {
  setText(it.name, b.online ? b.name.c_str() : (b.name + " · nedostupan").c_str());
  const int8_t shown = (b.online ? 1 : 0) | (b.active ? 2 : 0);
  if (shown == it.shown) return;
  it.shown = shown;
  const bool door = b.kind == BinaryKind::Door;
  const lv_color_t hot = door ? C_WARN : C_ON;
  const bool lit = b.online && b.active;
  lv_obj_set_style_bg_color(it.value, lit ? hot : b.online ? lv_color_hex(0x9AA4AF) : lv_color_hex(0x5A636E), 0);
  lv_obj_t* text = static_cast<lv_obj_t*>(lv_obj_get_user_data(it.root));
  setText(text, binaryStateText(b).c_str());
  setTextColor(text, lit ? hot : b.online ? WHITE : lv_color_hex(0x8A939E));
  lv_obj_set_style_border_color(it.root, lit ? hot : WHITE, 0);
  lv_obj_set_style_border_opa(it.root, lit ? 200 : 45, 0);
}

static Item makeLight(const SwitchState& sw, uint8_t style) {
  return makeRound(Kind::Light, sw.key(), style, &lv_font_montserrat_28, LV_SYMBOL_POWER, onLightClicked);
}

// Roletna: strelice gore/dole; dodir otvara prozorče sa Gore / Stop / Dole
static Item makeCover(const CoverState& c, uint8_t style) {
  return makeRound(Kind::Cover, c.key(), style, &lv_font_montserrat_20, LV_SYMBOL_UP "\n" LV_SYMBOL_DOWN,
                   onCoverClicked);
}

static void applyCover(Item& it, const CoverState& c) {
  setText(it.name, c.name.c_str());
  const int8_t shown = (c.online ? 1 : 0) | (c.pending ? 4 : 0);
  if (shown == it.shown) return;
  it.shown = shown;
  lv_obj_t* g = it.grip;
  lv_obj_set_style_shadow_width(g, 0, 0);
  lv_obj_set_style_border_color(g, c.pending ? C_ON : WHITE, 0);
  if (it.style == settings::STYLE_INVISIBLE) {
    lv_obj_set_style_bg_opa(g, 0, 0);
    lv_obj_set_style_border_opa(g, c.pending ? 160 : c.online ? 40 : 20, 0);
    return;
  }
  lv_obj_set_style_bg_color(g, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g, c.online ? 140 : 90, 0);
  lv_obj_set_style_border_opa(g, c.pending ? 220 : 45, 0);
  lv_obj_set_style_text_color(it.value, c.online ? WHITE : lv_color_hex(0x6E7782), 0);
}

static void applyLight(Item& it, const SwitchState& s) {
  setText(it.name, s.name.c_str());
  const int8_t shown = (s.online ? 1 : 0) | (s.on ? 2 : 0) | (s.pending ? 4 : 0);
  if (shown == it.shown) return;
  it.shown = shown;
  lv_obj_t* g = it.grip;
  if (it.style == settings::STYLE_INVISIBLE) {
    // Ugašeno: jedva vidljiv krug. Upaljeno: topao sjaj oko sijalice na slici.
    const bool lit = s.online && s.on;
    lv_obj_set_style_bg_color(g, C_ON, 0);
    lv_obj_set_style_bg_opa(g, lit ? 60 : 0, 0);
    lv_obj_set_style_border_color(g, s.pending ? C_ON : WHITE, 0);
    lv_obj_set_style_border_opa(g, s.pending ? 160 : lit ? 0 : s.online ? 40 : 20, 0);
    lv_obj_set_style_shadow_color(g, C_ON, 0);
    lv_obj_set_style_shadow_width(g, lit ? 44 : 0, 0);
    lv_obj_set_style_shadow_spread(g, lit ? 6 : 0, 0);
    lv_obj_set_style_shadow_opa(g, 120, 0);
    return;
  }
  if (s.online && s.on) {
    // Upaljeno: providno žuto (vidi se slika ispod), žuta ivica i blag sjaj
    lv_obj_set_style_bg_color(g, C_ON, 0);
    lv_obj_set_style_bg_opa(g, 115, 0);
    lv_obj_set_style_border_color(g, C_ON, 0);
    lv_obj_set_style_border_opa(g, 220, 0);
    lv_obj_set_style_shadow_color(g, C_ON, 0);
    lv_obj_set_style_shadow_width(g, 28, 0);
    lv_obj_set_style_shadow_spread(g, 2, 0);
    lv_obj_set_style_shadow_opa(g, 110, 0);
    lv_obj_set_style_text_color(it.value, WHITE, 0);
  } else {
    lv_obj_set_style_bg_color(g, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g, s.online ? 140 : 90, 0);
    lv_obj_set_style_border_color(g, WHITE, 0);
    lv_obj_set_style_border_opa(g, 45, 0);
    lv_obj_set_style_shadow_width(g, 0, 0);
    lv_obj_set_style_text_color(it.value, s.online ? WHITE : lv_color_hex(0x6E7782), 0);
  }
  lv_obj_set_style_opa(it.value, s.pending ? LV_OPA_50 : LV_OPA_COVER, 0);
}

// ---------------------------------------------------------------- katanac (alarm)

static void lockShape(bool open, lv_color_t c) {
  // Otključan: luk podignut iznad tela (vidi se razmak)
  const int32_t y = open ? 0 : 7;
  if (lv_obj_get_y(s_shackle) != y) lv_obj_set_y(s_shackle, y);
  setBorderColor(s_shackle, c);
  setBgColor(s_body, c);
}

static void updateLock(const std::vector<AlarmState>& alarms) {
  const AlarmState* a = nullptr;
  for (const AlarmState& x : alarms) {
    if (!settings::isHidden(x.key())) {
      a = &x;
      break;
    }
  }
  setVisible(s_lock, a != nullptr);
  if (!a) return;
  s_lockAlarm = a->deviceId;

  const uint32_t left = alarm_ui::exitSecondsLeft(*a);
  lv_color_t c = SOFT, bg = lv_color_black();
  lv_opa_t opa = 140;
  bool open = false;
  String text;
  if (!a->online) {
    c = lv_color_hex(0x8A939E);
    text = "Nedostupan";
  } else if (a->alarming) {
    c = WHITE;
    bg = lv_color_hex(0xC62828);
    opa = 235;
    text = a->mode == AlarmMode::Sos ? "SOS" : "ALARM";
  } else if (a->pending) {
    text = "Šaljem…";
    open = a->mode == AlarmMode::Disarmed;
  } else if (left) {
    c = C_ON;
    text = "Izlaz " + String(left) + " s";
  } else if (a->mode == AlarmMode::Disarmed) {
    c = C_OK;
    open = true;
    text = "Isključen";
  } else if (a->mode == AlarmMode::Home) {
    c = lv_color_hex(0x8EC5FF);
    text = "Kod kuće";
  } else if (a->mode == AlarmMode::Away) {
    c = lv_color_hex(0xFF6B6B);
    text = "Uključen";
  } else {
    text = "Nepoznato";
  }
  lockShape(open, c);
  setTextColor(s_lockText, c);
  setText(s_lockText, text.c_str());
  setBgColor(s_lock, bg);
  if (lv_obj_get_style_bg_opa(s_lock, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(s_lock, opa, 0);
}

// ---------------------------------------------------------------- sat i status

static void updateClock() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return;
  struct tm t;
  localtime_r(&now, &t);
  static const char* const DAYS[] = {"Nedelja", "Ponedeljak", "Utorak", "Sreda", "Četvrtak", "Petak", "Subota"};
  static const char* const MONTHS[] = {"januar", "februar", "mart",     "april",   "maj",      "jun",
                                       "jul",    "avgust",  "septembar", "oktobar", "novembar", "decembar"};
  char buf[48];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  setText(s_clock, buf);
  snprintf(buf, sizeof(buf), "%s, %d. %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
  setText(s_date, buf);
}

static void updateStatus(const SystemStatus& sys) {
  String msg;
  if (settings::get().wifiSsid.isEmpty()) msg = "Wi-Fi nije podešen — dodirni zupčanik";
  else if (!sys.wifiConnected) msg = "Nema Wi-Fi veze, povezujem…";
  else if (sys.tuya.state == LinkState::Error) msg = sys.tuya.message;
  else if (sys.ewelink.state == LinkState::Error) msg = sys.ewelink.message;
  setVisible(s_status, msg.length() > 0);
  if (msg.length()) setText(s_statusText, msg.c_str());
  setVisible(s_wifi, !sys.wifiConnected);
}

// ---------------------------------------------------------------- sinhronizacija

static void sync(bool force = false) {
  const std::vector<SensorInfo> sensors = g_store.sensors();
  const std::vector<SwitchState> lights = g_store.snapshot();
  const std::vector<CoverState> covers = g_store.covers();
  const std::vector<SmokeInfo> smokes = g_store.smokes();
  const std::vector<scenes::Scene> scs = scenes::all();
  const std::vector<ThermostatState> thermos = g_store.thermostats();
  const std::vector<BinarySensorState> bins = g_store.binaries();
  std::vector<settings::Position> pos = s_show ? std::vector<settings::Position>() : settings::positions();
  pos.insert(pos.end(), s_debugPos.begin(), s_debugPos.end());

  // Na slici je tačno ono što je u rasporedu (telefon: Raspored), istim redom; isti uređaj može više
  // puta. Uređaj isključen u aplikaciji se ne prikazuje (mesto u rasporedu ostaje).
  struct Want {
    Kind kind;
    int dev;  // indeks u sensors / lights
    uint16_t slot;
  };
  std::vector<Want> want;
  for (size_t p = 0; p < pos.size(); p++) {
    if (pos[p].page != s_page) continue;  // samo oznake prikazanog dela
    if (settings::isHidden(pos[p].key)) continue;  // isključen u aplikaciji: nema ga na slici (mesto ostaje)
    int dev = -1;
    if (scenes::isSceneKey(pos[p].key)) {
      for (size_t i = 0; dev < 0 && i < scs.size(); i++)
        if (scs[i].id == scenes::idOf(pos[p].key)) dev = i;
      if (dev >= 0) want.push_back({Kind::Scene, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < sensors.size(); i++)
      if (sensors[i].key() == pos[p].key) dev = i;
    if (dev >= 0) {
      want.push_back({Kind::Sensor, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < lights.size(); i++)
      if (lights[i].key() == pos[p].key) dev = i;
    if (dev >= 0) {
      want.push_back({Kind::Light, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < covers.size(); i++)
      if (covers[i].key() == pos[p].key) dev = i;
    if (dev >= 0) {
      want.push_back({Kind::Cover, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < smokes.size(); i++)
      if (smokes[i].key() == pos[p].key) dev = i;
    if (dev >= 0) {
      want.push_back({Kind::Smoke, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < thermos.size(); i++)
      if (thermos[i].key() == pos[p].key) dev = i;
    if (dev >= 0) {
      want.push_back({Kind::Thermo, dev, (uint16_t)p});
      continue;
    }
    for (size_t i = 0; dev < 0 && i < bins.size(); i++)
      if (bins[i].key() == pos[p].key) dev = i;
    if (dev >= 0) want.push_back({Kind::Binary, dev, (uint16_t)p});
  }

  bool same = s_items.size() == want.size();
  for (size_t i = 0; same && i < want.size(); i++) {
    const Item& it = s_items[i];
    same = it.kind == want[i].kind && it.slot == want[i].slot && it.key == pos[want[i].slot].key &&
           it.style == pos[want[i].slot].style;
  }
  if (!same) {
    lv_obj_clean(s_layer);
    s_items.clear();
    for (const Want& w : want) {
      s_items.push_back(w.kind == Kind::Sensor  ? makeSensor(sensors[w.dev])
                        : w.kind == Kind::Light ? makeLight(lights[w.dev], pos[w.slot].style)
                        : w.kind == Kind::Cover ? makeCover(covers[w.dev], pos[w.slot].style)
                        : w.kind == Kind::Smoke ? makeSmoke(smokes[w.dev])
                        : w.kind == Kind::Thermo ? makeThermo(thermos[w.dev])
                        : w.kind == Kind::Binary ? makeBinary(bins[w.dev])
                                                : makeScene(scs[w.dev]));
      s_items.back().slot = w.slot;
    }
  }
  for (size_t i = 0; i < want.size(); i++) {
    if (want[i].kind == Kind::Sensor) applySensor(s_items[i], sensors[want[i].dev]);
    else if (want[i].kind == Kind::Light) applyLight(s_items[i], lights[want[i].dev]);
    else if (want[i].kind == Kind::Cover) applyCover(s_items[i], covers[want[i].dev]);
    else if (want[i].kind == Kind::Smoke) applySmoke(s_items[i], smokes[want[i].dev]);
    else if (want[i].kind == Kind::Thermo) applyThermo(s_items[i], thermos[want[i].dev]);
    else if (want[i].kind == Kind::Binary) applyBinary(s_items[i], bins[want[i].dev]);
    else setText(s_items[i].name, scs[want[i].dev].name.c_str());
  }
  if (!same || force) {
    // Pozicije tek kad su tekstovi upisani (veličina zavisi od sadržaja)
    for (Item& it : s_items) placeItem(it, pos[it.slot]);
    if (s_edit) {
      for (Item& it : s_items) lv_obj_set_style_outline_width(it.grip, 2, 0);
    }
  }

  updateLock(g_store.alarms());
  updateStatus(g_store.systemStatus());
}

// ---------------------------------------------------------------- delovi (više fotografija)

static void showBackground() {
  const lv_image_dsc_t* img = background::load(s_page);
  if (img) lv_image_set_src(s_bg, img);
  setVisible(s_bg, img != nullptr);
  setVisible(s_noImg, img == nullptr);
  lv_obj_invalidate(s_scr);
}

static void updateDots() {
  const std::vector<String> names = s_show ? std::vector<String>{String()} : settings::pageNames();
  lv_obj_clean(s_dots);
  for (size_t i = 0; i < names.size(); i++) {
    lv_obj_t* d = box(s_dots);
    const bool cur = i == s_page;
    lv_obj_set_size(d, cur ? 22 : 9, 9);
    lv_obj_set_style_radius(d, 5, 0);
    lv_obj_set_style_bg_color(d, WHITE, 0);
    lv_obj_set_style_bg_opa(d, cur ? LV_OPA_COVER : 110, 0);
  }
  if (s_page < names.size() && names[s_page].length()) label(s_dots, F_SMALL, WHITE, names[s_page].c_str());
  const bool multi = names.size() > 1 && !s_edit;
  setVisible(s_dots, multi);
  setVisible(s_prev, multi);
  setVisible(s_next, multi);
  // Poruka o grešci dole levo ide desno od strelice
  lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, multi ? 16 + 54 + 12 : 16, -16);
}

void setPage(uint8_t page) {
  const uint8_t n = pageCount();
  if (page >= n) page = n - 1;
  if (page == s_page) return;
  s_page = page;
  showBackground();
  updateDots();
  sync(true);
}

uint8_t page() { return s_page; }

void debugPlace(const String& key, int16_t x, int16_t y) {
  settings::Position p;
  p.key = key;
  p.x = x;
  p.y = y;
  p.page = s_page;
  s_debugPos.push_back(p);
  sync(true);
}

void showcase(const std::vector<settings::Position>& pos) {
  s_show = true;
  s_debugPos = pos;
  background::setBuiltinOnly(true);
  s_page = 0;
  showBackground();
  updateDots();
  sync(true);
}

void update() {
  const uint32_t rev = g_store.revision(), hid = settings::hiddenRevision();
  const uint32_t uiRev = settings::uiRevision();
  if (uiRev != s_seenUi) {  // promena sa telefona: raspored / svetla / pozadina
    s_seenUi = uiRev;
    const uint8_t n = pageCount();
    if (s_page >= n) {  // deo je obrisan sa telefona
      s_page = n - 1;
      background::consumeChanged();
      showBackground();
    } else if (background::consumeChanged()) {
      showBackground();
    }
    updateDots();
    s_seenRev = rev;
    s_seenHidden = hid;
    sync(true);
  } else if (rev != s_seenRev || hid != s_seenHidden) {
    s_seenRev = rev;
    s_seenHidden = hid;
    sync();
  }
  if (millis() - s_lastTick >= 500) {
    s_lastTick = millis();
    updateClock();
    updateLock(g_store.alarms());  // odbrojavanje izlaznog kašnjenja
    // Uređivanje se samo završava posle 2 min bez dodira
    if (s_edit && lv_display_get_inactive_time(nullptr) > 120000) setEditMode(false);
  }
}

// ---------------------------------------------------------------- izgradnja

lv_obj_t* build() {
  s_scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x101820), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(s_scr, [](lv_event_t*) { setEditMode(true); }, LV_EVENT_LONG_PRESSED, nullptr);
  if (lv_indev_t* in = lv_indev_get_next(nullptr)) lv_indev_set_long_press_time(in, 800);
  // Prevlačenje levo/desno = sledeći/prethodni deo kuće (u krug)
  lv_obj_add_event_cb(s_scr, [](lv_event_t*) {
    const uint8_t n = pageCount();
    if (s_edit || n < 2) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d != LV_DIR_LEFT && d != LV_DIR_RIGHT) return;
    lv_indev_wait_release(lv_indev_active());  // bez "klika" na svetlo ispod prsta
    setPage(d == LV_DIR_LEFT ? (s_page + 1) % n : (s_page + n - 1) % n);
  }, LV_EVENT_GESTURE, nullptr);

  s_bg = lv_image_create(s_scr);
  lv_obj_set_pos(s_bg, 0, 0);
  lv_obj_remove_flag(s_bg, LV_OBJ_FLAG_CLICKABLE);
  s_noImg = label(s_scr, F_BODY, SOFT, "Ovaj deo još nema fotografiju.\nDodaj je sa telefona: Podešavanja → Web → Raspored → Fotografija.");
  lv_obj_set_style_text_align(s_noImg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(s_noImg);

  // Zatamnjenje gore, da se sat i dugmad čitaju preko svetle slike
  lv_obj_t* scrim = box(s_scr);
  lv_obj_set_size(scrim, LCD_H_RES, 150);
  lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_grad_color(scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_grad_dir(scrim, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_main_opa(scrim, 170, 0);
  lv_obj_set_style_bg_grad_opa(scrim, 0, 0);

  // Sloj sa temperaturama i svetlima (ispod gornje trake)
  s_layer = box(s_scr);
  lv_obj_set_size(s_layer, LCD_H_RES, LCD_V_RES);

  // Sat i datum
  s_clock = label(s_scr, F_HUGE, WHITE, "--:--");
  lv_obj_set_pos(s_clock, 24, 8);
  s_date = label(s_scr, F_BODY, SOFT);
  lv_obj_set_pos(s_date, 28, 70);

  // Desno: [wifi] [katanac] [lista] [podešavanja]
  lv_obj_t* right = box(s_scr);
  lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right, 10, 0);
  lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -16, 16);

  s_wifi = label(right, &lv_font_montserrat_24, C_ERR, LV_SYMBOL_WIFI);

  s_lock = box(right);
  lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_lock, LV_SIZE_CONTENT, 54);
  lv_obj_set_style_radius(s_lock, 27, 0);
  lv_obj_set_style_pad_left(s_lock, 14, 0);
  lv_obj_set_style_pad_right(s_lock, 18, 0);
  lv_obj_set_flex_flow(s_lock, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_lock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_lock, 10, 0);
  lv_obj_set_ext_click_area(s_lock, 6);
  glass(s_lock);
  lv_obj_add_event_cb(s_lock, [](lv_event_t*) {
    if (s_lockAlarm.length()) alarm_ui::open(s_lockAlarm);
  }, LV_EVENT_CLICKED, nullptr);
  // Katanac nacrtan od dva oblika: luk (okvir zaobljenog pravougaonika) i telo
  lv_obj_t* icon = box(s_lock);
  lv_obj_set_size(icon, 26, 36);
  s_shackle = box(icon);
  lv_obj_set_size(s_shackle, 18, 22);
  lv_obj_set_pos(s_shackle, 4, 7);
  lv_obj_set_style_radius(s_shackle, 9, 0);
  lv_obj_set_style_border_width(s_shackle, 4, 0);
  lv_obj_set_style_border_color(s_shackle, SOFT, 0);
  s_body = box(icon);
  lv_obj_set_size(s_body, 26, 18);
  lv_obj_set_pos(s_body, 0, 18);
  lv_obj_set_style_radius(s_body, 5, 0);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_body, SOFT, 0);
  s_lockText = label(s_lock, F_BODY, SOFT, "");

  roundButton(right, LV_SYMBOL_LIST, [](lv_event_t*) { ui::openList(); });
  roundButton(right, LV_SYMBOL_SETTINGS, [](lv_event_t*) { settings_ui::open(settings_ui::TAB_WIFI); });

  // Status (greška / nema veze) dole levo
  s_status = box(s_scr);
  lv_obj_set_size(s_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(s_status, 14, 0);
  lv_obj_set_style_pad_hor(s_status, 14, 0);
  lv_obj_set_style_pad_ver(s_status, 8, 0);
  glass(s_status, 190);
  lv_obj_set_style_border_color(s_status, C_ERR, 0);
  lv_obj_set_style_border_opa(s_status, LV_OPA_COVER, 0);
  s_statusText = label(s_status, F_SMALL, WHITE);
  lv_label_set_long_mode(s_statusText, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(s_statusText, 560, 0);
  lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 16, -16);
  lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

  // Tačkice delova (kad ih ima više), dole na sredini
  s_dots = box(s_scr);
  lv_obj_set_size(s_dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_dots, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_dots, 7, 0);
  lv_obj_set_style_pad_hor(s_dots, 12, 0);
  lv_obj_set_style_pad_ver(s_dots, 7, 0);
  lv_obj_set_style_radius(s_dots, 14, 0);
  glass(s_dots, 110);
  lv_obj_set_style_border_width(s_dots, 0, 0);
  lv_obj_align(s_dots, LV_ALIGN_BOTTOM_MID, 0, -12);

  // Strelice za prethodni/sledeći deo (pored prevlačenja), dole u uglovima
  s_prev = roundButton(s_scr, LV_SYMBOL_LEFT, [](lv_event_t*) {
    const uint8_t n = pageCount();
    if (n > 1) setPage((s_page + n - 1) % n);
  });
  lv_obj_align(s_prev, LV_ALIGN_BOTTOM_LEFT, 16, -16);
  s_next = roundButton(s_scr, LV_SYMBOL_RIGHT, [](lv_event_t*) {
    const uint8_t n = pageCount();
    if (n > 1) setPage((s_page + 1) % n);
  });
  lv_obj_align(s_next, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  showBackground();
  updateDots();

  // Traka za uređivanje rasporeda
  s_editBar = box(s_scr);
  lv_obj_set_size(s_editBar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(s_editBar, 30, 0);
  lv_obj_set_style_pad_left(s_editBar, 22, 0);
  lv_obj_set_style_pad_right(s_editBar, 8, 0);
  lv_obj_set_style_pad_ver(s_editBar, 8, 0);
  lv_obj_set_flex_flow(s_editBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_editBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_editBar, 16, 0);
  glass(s_editBar, 215);
  lv_obj_set_style_border_color(s_editBar, C_ON, 0);
  lv_obj_set_style_border_opa(s_editBar, LV_OPA_COVER, 0);
  label(s_editBar, F_BODY, WHITE, "Prevuci oznake na mesto");
  lv_obj_t* phone = box(s_editBar);
  lv_obj_add_flag(phone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(phone, LV_SIZE_CONTENT, 46);
  lv_obj_set_style_radius(phone, 23, 0);
  lv_obj_set_style_pad_hor(phone, 18, 0);
  glass(phone, 160);
  lv_obj_add_event_cb(phone, [](lv_event_t*) {
    setEditMode(false);
    settings_ui::open(settings_ui::TAB_WEB);  // QR kod za podešavanje sa telefona
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(phone, F_BODY, WHITE, "Na telefonu"));
  lv_obj_t* done = box(s_editBar);
  lv_obj_add_flag(done, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(done, LV_SIZE_CONTENT, 46);
  lv_obj_set_style_radius(done, 23, 0);
  lv_obj_set_style_pad_hor(done, 22, 0);
  lv_obj_set_style_bg_color(done, C_ON, 0);
  lv_obj_set_style_bg_opa(done, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(done, [](lv_event_t*) { setEditMode(false); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(done, F_BODY, C_ON_TEXT, "Gotovo"));
  lv_obj_align(s_editBar, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_add_flag(s_editBar, LV_OBJ_FLAG_HIDDEN);

  updateClock();
  sync();
  return s_scr;
}

}  // namespace home_ui
