#include "ui/ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "model/device_store.h"
#include "model/scenes.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "ui/alarm_screen.h"
#include "ui/cover_popup.h"
#include "ui/home_screen.h"
#include "ui/pin_prompt.h"
#include "ui/settings_screen.h"
#include "ui/smoke_alert.h"
#include "ui/thermo_popup.h"
#include "ui/theme.h"

namespace ui {

using namespace theme;

// ---------------------------------------------------------------- layout
static constexpr int HEADER_H = 76;
static constexpr int GRID_PAD = 16;
static constexpr int GAP = 12;
static constexpr int COLS = 4;
static constexpr int TILE_W = (LCD_H_RES - 2 * GRID_PAD - (COLS - 1) * GAP) / COLS;  // 183
static constexpr int TILE_H = 182;

struct Tile {
  String key;
  lv_obj_t* obj;
  lv_obj_t* icon;
  lv_obj_t* badge;
  lv_obj_t* name;
  lv_obj_t* state;
  int8_t shown = -1;  // poslednje iscrtano stanje (online/on/pending), -1 = još nije
};

static lv_obj_t* s_scr;   // lista (pločice)
static lv_obj_t* s_home;  // početni ekran
static lv_obj_t* s_grid;
static lv_obj_t* s_subtitle;
static lv_obj_t* s_clock;
static lv_obj_t* s_wifi;
static lv_obj_t* s_allOff;
static lv_obj_t* s_message;
static lv_obj_t* s_toast;
static lv_timer_t* s_toastTimer;
static lv_obj_t* s_chipDot[2];
static lv_obj_t* s_chip[2];
static std::vector<Tile> s_tiles;

// Alarm centrala: široka pločica (2 kolone) na početku mreže
struct AlarmTile {
  String id;
  lv_obj_t* obj;
  lv_obj_t* icon;
  lv_obj_t* name;
  lv_obj_t* state;
  lv_obj_t* detail;
};
static std::vector<AlarmTile> s_alarmTiles;

// Senzor temperature: samo trenutna vrednost
struct SensorTile {
  String id;
  lv_obj_t* obj;
  lv_obj_t* value;
  lv_obj_t* name;
  lv_obj_t* note;
};
static std::vector<SensorTile> s_sensorTiles;

// Roletna: naziv, položaj i dugmad Gore / Stop / Dole; dodir na pločicu otvara prozorče
struct CoverTile {
  String key;
  lv_obj_t* obj;
  lv_obj_t* name;
  lv_obj_t* state;
  int8_t shown = -2;
};
static std::vector<CoverTile> s_coverTiles;

// Scena: dodir pokreće
struct SceneTile {
  String id;
  lv_obj_t* obj;
  lv_obj_t* name;
  lv_obj_t* note;
};
static std::vector<SceneTile> s_sceneTiles;
static uint32_t s_seenScenes = 0;

// Detektor dima: samo prikaz stanja (crvena pločica kad javlja dim)
struct SmokeTile {
  String id;
  lv_obj_t* obj;
  lv_obj_t* icon;
  lv_obj_t* state;
  lv_obj_t* name;
  lv_obj_t* note;
};
static std::vector<SmokeTile> s_smokeTiles;

// Termostat: trenutna i željena temperatura; dodir otvara prozorče sa − / +
struct ThermoTile {
  String key;
  lv_obj_t* obj;
  lv_obj_t* value;
  lv_obj_t* target;
  lv_obj_t* name;
  lv_obj_t* note;
};
static std::vector<ThermoTile> s_thermoTiles;

// Senzor vrata / pokreta: samo prikaz stanja
struct BinaryTile {
  String key;
  lv_obj_t* obj;
  lv_obj_t* state;
  lv_obj_t* detail;
  lv_obj_t* name;
  lv_obj_t* note;
};
static std::vector<BinaryTile> s_binaryTiles;
static uint32_t s_seenRevision = 0;
static uint32_t s_seenHidden = 0;
static uint32_t s_seenToast = 0;
static SystemStatus s_sys;

// ---------------------------------------------------------------- toast

static void hideToast(lv_timer_t*) { lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN); }

void showToast(const String& text, bool error) {
  lv_obj_set_style_border_color(s_toast, error ? C_ERR : C_ON, 0);
  lv_label_set_text(s_toast, text.c_str());
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_toast);
  lv_timer_reset(s_toastTimer);
  lv_timer_resume(s_toastTimer);
}

// ---------------------------------------------------------------- događaji

static void onTileClicked(lv_event_t* e) {
  const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
  if (idx >= s_tiles.size()) return;
  toggleSwitch(s_tiles[idx].key);
}

void toggleSwitch(const String& key) {
  for (const auto& s : g_store.snapshot()) {
    if (s.key() != key) continue;
    if (!s.online) {
      showToast(s.name + " je trenutno nedostupan");
    } else if (!s.pending) {
      cloud::requestSet(key, !s.on);
    }
    break;
  }
}

// Samo svetla i zidni prekidači (ne utičnice: pumpa, grejač...)
// ---------------------------------------------------------------- scene

// Šalje samo komande koje nešto menjaju (uređaj koji je već u tom stanju se preskače)
static void execScene(const scenes::Scene& sc) {
  const std::vector<SwitchState> sw = g_store.snapshot();
  const std::vector<AlarmState> alarms = g_store.alarms();
  const std::vector<CoverState> covers = g_store.covers();
  const std::vector<ThermostatState> thermos = g_store.thermostats();
  int sent = 0, failed = 0, same = 0, off = 0;
  for (const scenes::Action& a : sc.actions) {
    if (settings::isHidden(a.key)) {  // isključen u aplikaciji: nikakva komanda
      off++;
      continue;
    }
    // Termostat: "on" / "off" ili "t21.5" (uključi i podesi temperaturu)
    auto th = std::find_if(thermos.begin(), thermos.end(), [&](const ThermostatState& t) { return t.key() == a.key; });
    if (th != thermos.end()) {
      if (!th->online) {
        failed++;
      } else if (a.op == "on" || a.op == "off") {
        const bool target = a.op == "on";
        if (!th->hasPower || th->on == target) same++;
        else if (cloud::requestThermostat(a.key, target ? 1 : 0, NAN)) sent++;
        else failed++;
      } else if (a.op.startsWith("t")) {
        const float v = thermoClamp(*th, a.op.substring(1).toFloat());
        const bool turnOn = th->hasPower && !th->on;
        if (!turnOn && th->hasTarget && fabsf(th->target - v) < 0.05f) same++;
        else if (cloud::requestThermostat(a.key, turnOn ? 1 : -1, v)) sent++;
        else failed++;
      }
      continue;
    }
    if (a.op == "on" || a.op == "off") {
      const bool target = a.op == "on";
      auto it = std::find_if(sw.begin(), sw.end(), [&](const SwitchState& s) { return s.key() == a.key; });
      if (it == sw.end() || !it->online) failed++;
      else if (it->on == target && !it->pending) same++;
      else if (cloud::requestSet(a.key, target)) sent++;
      else failed++;
    } else if (a.op == "open" || a.op == "close" || a.op == "stop") {
      const CoverAction act = a.op == "open" ? CoverAction::Open : a.op == "close" ? CoverAction::Close : CoverAction::Stop;
      auto it = std::find_if(covers.begin(), covers.end(), [&](const CoverState& c) { return c.key() == a.key; });
      if (it == covers.end() || !it->online) failed++;
      else if (cloud::requestCover(a.key, act)) sent++;
      else failed++;
    } else if (a.op == "away" || a.op == "home" || a.op == "disarm") {
      const AlarmMode mode = a.op == "away" ? AlarmMode::Away : a.op == "home" ? AlarmMode::Home : AlarmMode::Disarmed;
      auto it = std::find_if(alarms.begin(), alarms.end(), [&](const AlarmState& x) { return x.deviceId == a.key; });
      if (it == alarms.end() || !it->online) failed++;
      else if (it->mode == mode) same++;
      else if (cloud::requestAlarm(a.key, mode)) sent++;
      else failed++;
    }
  }
  Serial.printf("[scene] %s: poslato %d, već tako %d, nije uspelo %d, isključeno %d\n", sc.name.c_str(), sent, same,
                failed, off);
  String msg = "„" + sc.name + "“: ";
  if (sent) msg += String(sent) + (sent == 1 ? " komanda poslata" : " komandi poslato");
  else if (!failed && same) msg += "sve je već tako podešeno";
  else if (!failed) msg += "nema uključenih uređaja u sceni";
  if (failed) msg += String(sent ? ", " : "") + String(failed) + " uređaj(a) nedostupno";
  showToast(msg, failed > 0);
}

void runScene(const String& id) {
  scenes::Scene sc;
  if (!scenes::find(id, sc) || settings::isHidden(scenes::keyFor(id))) return;
  // Isključivanje uključenog alarma traži PIN, isto kao na ekranu alarma
  bool needsPin = false;
  for (const AlarmState& a : g_store.alarms())
    for (const scenes::Action& x : sc.actions)
      if (x.op == "disarm" && x.key == a.deviceId && alarmArmed(a.mode)) needsPin = true;
  if (needsPin && settings::hasAlarmPin()) {
    lv_obj_t* back = lv_screen_active();
    pin_prompt::ask("Scena „" + sc.name + "“", [id, back]() {
      lv_screen_load(back);
      scenes::Scene s2;
      if (scenes::find(id, s2)) execScene(s2);
    });
    return;
  }
  execScene(sc);
}

static void onAllOff(lv_event_t*) {
  int n = 0;
  for (const auto& s : g_store.snapshot()) {
    if (groupIsLighting(s.group) && s.online && s.on && !s.pending && !settings::isHidden(s.key()) &&
        cloud::requestSet(s.key(), false))
      n++;
  }
  if (n == 0) showToast("Sva svetla su već ugašena");
}

static void onChipClicked(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const CloudStatus& cs = i == 0 ? s_sys.tuya : s_sys.ewelink;
  const char* name = i == 0 ? "Tuya" : "eWeLink";
  String t;
  switch (cs.state) {
    case LinkState::Ok: t = String(name) + ": povezano"; break;
    case LinkState::Connecting: t = String(name) + ": povezivanje…"; break;
    case LinkState::Disabled: t = String(name) + ": " + cs.message + " (Podešavanja)"; break;
    case LinkState::Error: t = cs.message; break;
  }
  showToast(t);
}

// ---------------------------------------------------------------- građenje ekrana

static lv_obj_t* makeChip(lv_obj_t* parent, const char* text, int idx) {
  lv_obj_t* chip = lv_obj_create(parent);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, 34);
  lv_obj_set_style_pad_hor(chip, 12, 0);
  lv_obj_set_style_radius(chip, 17, 0);
  lv_obj_set_style_bg_color(chip, C_CARD, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chip, 8, 0);
  lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(chip, onChipClicked, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

  lv_obj_t* dot = lv_obj_create(chip);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, C_MUTED, 0);

  lv_obj_t* l = lv_label_create(chip);
  lv_obj_set_style_text_font(l, F_SMALL, 0);
  lv_obj_set_style_text_color(l, C_TEXT, 0);
  lv_label_set_text(l, text);

  s_chipDot[idx] = dot;
  s_chip[idx] = chip;
  return chip;
}

static void buildHeader() {
  lv_obj_t* hdr = lv_obj_create(s_scr);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, LCD_H_RES, HEADER_H);
  lv_obj_set_style_pad_hor(hdr, 20, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  // Nazad na početni ekran
  lv_obj_t* back = lv_button_create(hdr);
  lv_obj_set_size(back, 48, 48);
  lv_obj_set_style_radius(back, 24, 0);
  lv_obj_set_style_bg_color(back, C_CARD, 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x2A323C), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_ext_click_area(back, 8);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*) { lv_screen_load(s_home); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backIco = lv_label_create(back);
  lv_obj_set_style_text_font(backIco, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(backIco, C_TEXT, 0);
  lv_label_set_text(backIco, LV_SYMBOL_LEFT);
  lv_obj_center(backIco);

  lv_obj_t* title = lv_label_create(hdr);
  lv_obj_set_style_text_font(title, F_TITLE, 0);
  lv_obj_set_style_text_color(title, C_TEXT, 0);
  lv_label_set_text(title, PANEL_TITLE);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 62, 10);

  s_subtitle = lv_label_create(hdr);
  lv_obj_set_style_text_font(s_subtitle, F_SMALL, 0);
  lv_obj_set_style_text_color(s_subtitle, C_MUTED, 0);
  lv_label_set_text(s_subtitle, "");
  // Kratko i ograničeno: sa oba clouda dugme „Ugasi svetla“ počinje odmah desno od naslova
  lv_label_set_long_mode(s_subtitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_subtitle, 130);
  lv_obj_align(s_subtitle, LV_ALIGN_TOP_LEFT, 64, 48);

  // Desna strana: [Sve ugasi] [Tuya] [eWeLink] [wifi] [sat]
  lv_obj_t* right = lv_obj_create(hdr);
  lv_obj_remove_style_all(right);
  lv_obj_set_size(right, LV_SIZE_CONTENT, HEADER_H);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  // START (ne END): kod LV_SIZE_CONTENT kontejnera END pomera decu van izračunate širine -> odsečeno.
  // Desno poravnanje već daje lv_obj_align(RIGHT_MID) samog kontejnera.
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right, 10, 0);
  lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* allOff = s_allOff = lv_button_create(right);
  lv_obj_set_height(allOff, 44);
  lv_obj_set_style_radius(allOff, 22, 0);
  lv_obj_set_style_bg_color(allOff, C_CARD, 0);
  lv_obj_set_style_bg_color(allOff, lv_color_hex(0x2A323C), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(allOff, 0, 0);
  lv_obj_set_style_pad_hor(allOff, 16, 0);
  lv_obj_set_flex_flow(allOff, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(allOff, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(allOff, 8, 0);
  lv_obj_add_event_cb(allOff, onAllOff, LV_EVENT_CLICKED, nullptr);
  // Simbol iz ugrađenog fonta, tekst iz TTF-a (TinyTTF nema LVGL simbole)
  lv_obj_t* ico = lv_label_create(allOff);
  lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(ico, C_TEXT, 0);
  lv_label_set_text(ico, LV_SYMBOL_POWER);
  lv_obj_t* lbl = lv_label_create(allOff);
  lv_obj_set_style_text_font(lbl, F_SMALL, 0);
  lv_obj_set_style_text_color(lbl, C_TEXT, 0);
  lv_label_set_text(lbl, "Ugasi svetla");

  makeChip(right, "Tuya", 0);
  makeChip(right, "eWeLink", 1);

  s_wifi = lv_label_create(right);
  lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_24, 0);
  lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(s_wifi, C_MUTED, 0);

  s_clock = lv_label_create(right);
  lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(s_clock, C_TEXT, 0);
  lv_label_set_text(s_clock, "--:--");

  lv_obj_t* gear = lv_button_create(right);
  lv_obj_set_size(gear, 48, 48);
  lv_obj_set_style_radius(gear, 24, 0);
  lv_obj_set_style_bg_color(gear, C_CARD, 0);
  lv_obj_set_style_bg_color(gear, lv_color_hex(0x2A323C), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(gear, 0, 0);
  lv_obj_set_ext_click_area(gear, 8);
  lv_obj_add_event_cb(gear, [](lv_event_t*) { settings_ui::open(0); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* gi = lv_label_create(gear);
  lv_obj_set_style_text_font(gi, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(gi, C_TEXT, 0);
  lv_label_set_text(gi, LV_SYMBOL_SETTINGS);
  lv_obj_center(gi);
}

static void buildBody() {
  s_grid = lv_obj_create(s_scr);
  lv_obj_remove_style_all(s_grid);
  lv_obj_set_size(s_grid, LCD_H_RES, LCD_V_RES - HEADER_H);
  lv_obj_set_pos(s_grid, 0, HEADER_H);
  lv_obj_set_style_pad_hor(s_grid, GRID_PAD, 0);
  lv_obj_set_style_pad_top(s_grid, 4, 0);
  lv_obj_set_style_pad_bottom(s_grid, GRID_PAD, 0);
  lv_obj_set_style_pad_row(s_grid, GAP, 0);
  lv_obj_set_style_pad_column(s_grid, GAP, 0);
  lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_ACTIVE);

  s_message = lv_label_create(s_scr);
  lv_obj_set_width(s_message, LCD_H_RES - 120);
  lv_obj_set_style_text_font(s_message, F_NAME, 0);
  lv_obj_set_style_text_color(s_message, C_MUTED, 0);
  lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_message, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_message, LV_ALIGN_CENTER, 0, HEADER_H / 2);
  lv_label_set_text(s_message, "Pokretanje…");

  s_toast = lv_label_create(lv_layer_top());  // vidi se preko svih ekrana
  lv_obj_set_style_text_font(s_toast, F_SMALL, 0);
  lv_obj_set_style_text_color(s_toast, C_TEXT, 0);
  lv_obj_set_style_bg_color(s_toast, C_TOAST, 0);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_toast, C_ERR, 0);
  lv_obj_set_style_border_width(s_toast, 1, 0);
  lv_obj_set_style_radius(s_toast, 12, 0);
  lv_obj_set_style_pad_hor(s_toast, 18, 0);
  lv_obj_set_style_pad_ver(s_toast, 12, 0);
  lv_obj_set_style_max_width(s_toast, LCD_H_RES - 80, 0);
  lv_label_set_long_mode(s_toast, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toastTimer = lv_timer_create(hideToast, 5000, nullptr);
  lv_timer_pause(s_toastTimer);
}

static Tile makeTile(size_t idx, const SwitchState& s) {
  Tile t;
  t.key = s.key();
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_set_style_border_color(t.obj, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(t.obj, 3, LV_STATE_PRESSED);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(t.obj, onTileClicked, LV_EVENT_CLICKED, (void*)(uintptr_t)idx);

  t.icon = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.icon, &lv_font_montserrat_40, 0);
  lv_label_set_text(t.icon, LV_SYMBOL_POWER);
  lv_obj_align(t.icon, LV_ALIGN_TOP_LEFT, 0, 0);

  t.badge = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.badge, F_SMALL, 0);
  lv_label_set_text(t.badge, cloudName(s.cloud));
  lv_obj_align(t.badge, LV_ALIGN_TOP_RIGHT, 0, 2);

  t.state = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.state, F_SMALL, 0);
  lv_obj_align(t.state, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_set_height(t.name, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(t.name, 2 * lv_font_get_line_height(F_NAME) + 2, 0);
  lv_label_set_text(t.name, s.name.c_str());
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applyTile(Tile& t, const SwitchState& s) {
  lv_color_t bg, fg, muted, icon;
  const char* state;
  if (!s.online) {
    bg = C_CARD_OFFLINE; fg = C_MUTED; muted = lv_color_hex(0x4A535E); icon = lv_color_hex(0x343C46);
    state = "Nedostupno";
  } else if (s.on) {
    bg = C_ON; fg = C_ON_TEXT; muted = C_ON_MUTED; icon = C_ON_TEXT;
    state = "Uključeno";
  } else {
    bg = C_CARD; fg = C_TEXT; muted = C_MUTED; icon = lv_color_hex(0x5B6673);
    state = "Isključeno";
  }
  if (s.pending) state = "Šaljem…";

  if (strcmp(lv_label_get_text(t.name), s.name.c_str()) != 0) lv_label_set_text(t.name, s.name.c_str());
  // Svaka promena stila prekrtava pločicu (opterećuje PSRAM -> smetnje na ekranu), pa samo kad treba
  const int8_t shown = (s.online ? 1 : 0) | (s.on ? 2 : 0) | (s.pending ? 4 : 0);
  if (shown == t.shown) return;
  t.shown = shown;

  lv_obj_set_style_bg_color(t.obj, bg, 0);
  lv_obj_set_style_text_color(t.name, fg, 0);
  lv_obj_set_style_text_color(t.state, muted, 0);
  lv_obj_set_style_text_color(t.badge, muted, 0);
  lv_obj_set_style_text_color(t.icon, icon, 0);
  lv_obj_set_style_opa(t.icon, s.pending ? LV_OPA_50 : LV_OPA_COVER, 0);
  if (strcmp(lv_label_get_text(t.state), state) != 0) lv_label_set_text(t.state, state);
}

// ---------------------------------------------------------------- izmene bez nepotrebnog prekrtavanja

static void setText(lv_obj_t* l, const char* text) {
  if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

static void setVisible(lv_obj_t* o, bool visible) {
  if (visible == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void setTextColor(lv_obj_t* o, lv_color_t c) {
  if (lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) return;
  lv_obj_set_style_text_color(o, c, 0);
}

// ---------------------------------------------------------------- senzor temperature

static SensorTile makeSensorTile(const SensorInfo& x) {
  SensorTile t;
  t.id = x.deviceId;
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);  // samo prikaz

  t.value = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.value, F_HUGE, 0);
  lv_obj_align(t.value, LV_ALIGN_TOP_LEFT, 0, 0);

  t.note = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.note, F_SMALL, 0);
  lv_obj_align(t.note, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_height(t.name, 2 * lv_font_get_line_height(F_NAME) + 2, 0);
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applySensorTile(SensorTile& t, const SensorInfo& x) {
  char buf[16] = "--";
  if (x.hasValue) {
    snprintf(buf, sizeof(buf), "%.1f°", x.value);
    if (char* dot = strchr(buf, '.')) *dot = ',';  // decimalni zarez
  }
  setText(t.value, buf);
  setText(t.name, x.name.c_str());
  setText(t.note, x.online ? (x.fahrenheit ? "Temperatura (°F)" : "Temperatura") : "Nedostupan");
  setTextColor(t.value, x.online ? C_TEXT : C_MUTED);
  setTextColor(t.name, x.online ? C_TEXT : C_MUTED);
  setTextColor(t.note, C_MUTED);
}

// ---------------------------------------------------------------- roletna

struct CoverBtn {
  String key;
  CoverAction action;
};

static CoverTile makeCoverTile(const CoverState& c) {
  CoverTile t;
  t.key = c.key();
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_set_style_border_color(t.obj, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(t.obj, 3, LV_STATE_PRESSED);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  char* key = strdup(t.key.c_str());
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { cover_ui::open(static_cast<const char*>(lv_event_get_user_data(e))); },
                      LV_EVENT_CLICKED, key);
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, key);

  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_height(t.name, 2 * lv_font_get_line_height(F_NAME) + 2, 0);
  lv_obj_align(t.name, LV_ALIGN_TOP_LEFT, 0, 0);

  t.state = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.state, F_SMALL, 0);
  lv_obj_align(t.state, LV_ALIGN_BOTTOM_LEFT, 0, -62);

  // Gore / Stop / Dole direktno na pločici
  static const char* const SYM[] = {LV_SYMBOL_UP, LV_SYMBOL_STOP, LV_SYMBOL_DOWN};
  static const CoverAction ACT[] = {CoverAction::Open, CoverAction::Stop, CoverAction::Close};
  constexpr int BW = (TILE_W - 28 - 2 * 6) / 3;
  for (int i = 0; i < 3; i++) {
    lv_obj_t* b = lv_obj_create(t.obj);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BW, 54);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * (BW + 6), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x262E38), 0);
    lv_obj_set_style_bg_color(b, C_ON, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, C_TEXT, 0);
    lv_obj_set_style_text_color(b, C_ON_TEXT, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_label_set_text(l, SYM[i]);
    lv_obj_center(l);
    CoverBtn* cb = new CoverBtn{t.key, ACT[i]};
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      const CoverBtn* c = static_cast<const CoverBtn*>(lv_event_get_user_data(e));
      cover_ui::command(c->key, c->action);
    }, LV_EVENT_CLICKED, cb);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { delete static_cast<CoverBtn*>(lv_event_get_user_data(e)); },
                        LV_EVENT_DELETE, cb);
  }
  return t;
}

static void applyCoverTile(CoverTile& t, const CoverState& c) {
  setText(t.name, c.name.c_str());
  String st;
  if (!c.online) st = "Nedostupna";
  else if (c.pending) st = "Šaljem…";
  else if (c.position >= 0) st = "Položaj " + String(c.position) + " %";
  else st = "Roletna";
  setText(t.state, st.c_str());
  setTextColor(t.name, c.online ? C_TEXT : C_MUTED);
  setTextColor(t.state, C_MUTED);
}

// ---------------------------------------------------------------- scena

static SceneTile makeSceneTile(const scenes::Scene& sc) {
  SceneTile t;
  t.id = sc.id;
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, lv_color_hex(0x1F2A3A), 0);
  lv_obj_set_style_bg_color(t.obj, C_ON, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  char* id = strdup(sc.id.c_str());
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { runScene(static_cast<const char*>(lv_event_get_user_data(e))); },
                      LV_EVENT_CLICKED, id);
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, id);
  lv_obj_t* icon = lv_label_create(t.obj);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(icon, C_ON, 0);
  lv_label_set_text(icon, LV_SYMBOL_PLAY);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
  t.note = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.note, F_SMALL, 0);
  lv_obj_set_style_text_color(t.note, C_MUTED, 0);
  lv_obj_align(t.note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_style_text_color(t.name, C_TEXT, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_height(t.name, 2 * lv_font_get_line_height(F_NAME) + 2, 0);
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applySceneTile(SceneTile& t, const scenes::Scene& sc) {
  setText(t.name, sc.name.c_str());
  const String note = "Scena · " + String(sc.actions.size()) + (sc.actions.size() == 1 ? " uređaj" : " uređaja");
  setText(t.note, note.c_str());
}

// ---------------------------------------------------------------- detektor dima

static SmokeTile makeSmokeTile(const SmokeInfo& x) {
  SmokeTile t;
  t.id = x.deviceId;
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);  // samo prikaz

  t.icon = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.icon, &lv_font_montserrat_40, 0);
  lv_obj_align(t.icon, LV_ALIGN_TOP_LEFT, 0, 0);
  t.state = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.state, F_NAME, 0);
  lv_obj_set_width(t.state, TILE_W - 28);
  lv_label_set_long_mode(t.state, LV_LABEL_LONG_WRAP);
  lv_obj_align(t.state, LV_ALIGN_TOP_LEFT, 0, 50);
  t.note = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.note, F_SMALL, 0);
  lv_obj_align(t.note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applySmokeTile(SmokeTile& t, const SmokeInfo& x) {
  static const lv_color_t RED = lv_color_hex(0xB3261E);
  const bool warn = x.online && (x.fault || x.tamper || x.batteryLow);
  const lv_color_t bg = x.online && x.alarm ? RED : x.online ? C_CARD : C_CARD_OFFLINE;
  if (!lv_color_eq(lv_obj_get_style_bg_color(t.obj, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(t.obj, bg, 0);
  setText(t.icon, x.online && !x.alarm && !warn ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
  setTextColor(t.icon, !x.online ? C_MUTED : x.alarm ? lv_color_white() : warn ? C_WARN : C_OK);
  setText(t.state, smokeStateText(x).c_str());
  setTextColor(t.state, x.alarm && x.online ? lv_color_white() : x.online ? C_TEXT : C_MUTED);
  setText(t.name, x.name.c_str());
  setTextColor(t.name, x.alarm && x.online ? lv_color_white() : x.online ? C_TEXT : C_MUTED);
  setText(t.note, x.battery >= 0 ? ("Detektor dima · " + String(x.battery) + "%").c_str() : "Detektor dima");
  setTextColor(t.note, x.alarm && x.online ? lv_color_hex(0xFFD9D6) : C_MUTED);
}

// ---------------------------------------------------------------- termostat

static ThermoTile makeThermoTile(const ThermostatState& x) {
  ThermoTile t;
  t.key = x.key();
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_set_style_border_color(t.obj, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(t.obj, 3, LV_STATE_PRESSED);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  char* key = strdup(t.key.c_str());
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { thermo_ui::open(static_cast<const char*>(lv_event_get_user_data(e))); },
                      LV_EVENT_CLICKED, key);
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, key);

  t.value = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.value, F_HUGE, 0);
  lv_obj_align(t.value, LV_ALIGN_TOP_LEFT, 0, -4);
  t.target = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.target, F_BODY, 0);
  lv_obj_align(t.target, LV_ALIGN_TOP_LEFT, 0, 58);
  t.note = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.note, F_SMALL, 0);
  lv_obj_align(t.note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applyThermoTile(ThermoTile& t, const ThermostatState& x) {
  char cur[16] = "--";
  if (x.hasCurrent) {
    snprintf(cur, sizeof(cur), "%.1f°", x.current);
    if (char* dot = strchr(cur, '.')) *dot = ',';
  }
  setText(t.value, cur);
  String tg;
  if (!x.online) tg = "";
  else if (!x.on) tg = "Isključen";
  else if (x.pending) tg = "Šaljem…";
  else if (x.hasTarget) tg = "→ " + thermoDegrees(x.target);
  setText(t.target, tg.c_str());
  setText(t.name, x.name.c_str());
  String note = !x.online ? "Nedostupan" : x.on && x.heating == 1 ? "Termostat · greje" : "Termostat";
  setText(t.note, note.c_str());
  setTextColor(t.value, x.online && x.on ? C_TEXT : C_MUTED);
  setTextColor(t.target, x.on && x.heating == 1 ? C_ON : C_MUTED);
  setTextColor(t.name, x.online ? C_TEXT : C_MUTED);
  setTextColor(t.note, x.online && x.on && x.heating == 1 ? C_ON : C_MUTED);
  const lv_color_t bg = x.online ? C_CARD : C_CARD_OFFLINE;
  if (!lv_color_eq(lv_obj_get_style_bg_color(t.obj, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(t.obj, bg, 0);
}

// ---------------------------------------------------------------- senzor vrata / pokreta

static BinaryTile makeBinaryTile(const BinarySensorState& x) {
  BinaryTile t;
  t.key = x.key();
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, TILE_W, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_border_width(t.obj, 0, 0);
  lv_obj_set_style_pad_all(t.obj, 14, 0);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);  // samo prikaz

  t.state = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.state, F_TITLE, 0);
  lv_obj_align(t.state, LV_ALIGN_TOP_LEFT, 0, 0);
  t.detail = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.detail, F_SMALL, 0);
  lv_obj_set_width(t.detail, TILE_W - 28);
  lv_label_set_long_mode(t.detail, LV_LABEL_LONG_WRAP);
  lv_obj_align(t.detail, LV_ALIGN_TOP_LEFT, 0, 40);
  t.note = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.note, F_SMALL, 0);
  lv_obj_align(t.note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_NAME, 0);
  lv_obj_set_width(t.name, TILE_W - 28);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_align(t.name, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  return t;
}

static void applyBinaryTile(BinaryTile& t, const BinarySensorState& x) {
  const bool lit = x.online && x.active;
  const lv_color_t hot = x.kind == BinaryKind::Door ? C_WARN : C_ON;
  setText(t.state, binaryStateText(x).c_str());
  setTextColor(t.state, lit ? hot : x.online ? C_TEXT : C_MUTED);
  setText(t.detail, binaryNote(x).c_str());
  setTextColor(t.detail, x.online && (x.batteryLow || x.tamper) ? C_WARN : C_MUTED);
  setText(t.name, x.name.c_str());
  setTextColor(t.name, x.online ? C_TEXT : C_MUTED);
  const String note = String(binaryKindName(x.kind)) + (x.battery >= 0 ? " · " + String(x.battery) + "%" : "");
  setText(t.note, note.c_str());
  setTextColor(t.note, C_MUTED);
  const lv_color_t bg = x.online ? C_CARD : C_CARD_OFFLINE;
  if (!lv_color_eq(lv_obj_get_style_bg_color(t.obj, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(t.obj, bg, 0);
  const int32_t bw = lit ? 2 : 0;
  if (lv_obj_get_style_border_width(t.obj, LV_PART_MAIN) != bw) {
    lv_obj_set_style_border_color(t.obj, hot, 0);
    lv_obj_set_style_border_width(t.obj, bw, 0);
  }
}

// ---------------------------------------------------------------- alarm pločica

static AlarmTile makeAlarmTile(const AlarmState& a) {
  AlarmTile t;
  t.id = a.deviceId;
  t.obj = lv_obj_create(s_grid);
  lv_obj_remove_style_all(t.obj);
  lv_obj_set_size(t.obj, 2 * TILE_W + GAP, TILE_H);
  lv_obj_set_style_radius(t.obj, 20, 0);
  lv_obj_set_style_bg_opa(t.obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(t.obj, C_CARD, 0);
  lv_obj_set_style_pad_all(t.obj, 16, 0);
  lv_obj_set_style_border_color(t.obj, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(t.obj, 3, LV_STATE_PRESSED);
  lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
  char* id = strdup(a.deviceId.c_str());  // oslobađa se kad se pločica obriše
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { alarm_ui::open(static_cast<const char*>(lv_event_get_user_data(e))); },
                      LV_EVENT_CLICKED, id);
  lv_obj_add_event_cb(t.obj, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, id);

  t.icon = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.icon, &lv_font_montserrat_40, 0);
  lv_label_set_text(t.icon, LV_SYMBOL_BELL);
  lv_obj_align(t.icon, LV_ALIGN_TOP_LEFT, 0, 0);

  t.name = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.name, F_SMALL, 0);
  lv_obj_set_width(t.name, 2 * TILE_W + GAP - 110);
  lv_label_set_long_mode(t.name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(t.name, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(t.name, LV_ALIGN_TOP_RIGHT, 0, 2);

  t.state = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.state, F_TITLE, 0);
  lv_obj_align(t.state, LV_ALIGN_BOTTOM_LEFT, 0, -26);

  t.detail = lv_label_create(t.obj);
  lv_obj_set_style_text_font(t.detail, F_SMALL, 0);
  lv_obj_set_width(t.detail, 2 * TILE_W + GAP - 32);
  lv_label_set_long_mode(t.detail, LV_LABEL_LONG_DOT);
  lv_obj_align(t.detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return t;
}

static void applyAlarmTile(AlarmTile& t, const AlarmState& a) {
  const alarm_ui::Look l = alarm_ui::lookFor(a);
  if (!lv_color_eq(lv_obj_get_style_bg_color(t.obj, LV_PART_MAIN), l.bg)) lv_obj_set_style_bg_color(t.obj, l.bg, 0);
  setTextColor(t.icon, l.accent);
  setTextColor(t.name, l.fg);
  setTextColor(t.state, l.fg);
  setTextColor(t.detail, l.fg);
  setText(t.name, ("Alarm · " + a.name).c_str());
  setText(t.state, l.state.c_str());
  setText(t.detail, l.detail.c_str());
}

// ---------------------------------------------------------------- sinhronizacija sa store-om

static void setChip(int i, const CloudStatus& cs) {
  lv_color_t c = C_MUTED;
  switch (cs.state) {
    case LinkState::Ok: c = C_OK; break;
    case LinkState::Connecting: c = C_WARN; break;
    case LinkState::Error: c = C_ERR; break;
    case LinkState::Disabled: break;
  }
  if (!lv_color_eq(lv_obj_get_style_bg_color(s_chipDot[i], LV_PART_MAIN), c))
    lv_obj_set_style_bg_color(s_chipDot[i], c, 0);
  setVisible(s_chip[i], cs.state != LinkState::Disabled);
}

static String statusMessage(const std::vector<SwitchState>& items, size_t hiddenCount) {
  if (!items.empty()) return "";
  if (hiddenCount) return "Svi prekidači su sakriveni.\nPrikaži ih u Podešavanja → Uređaji.";
  if (!s_sys.wifiConnected)
    return settings::get().wifiSsid.isEmpty() ? "Wi-Fi mreža nije podešena.\nOtvori podešavanja (zupčanik gore desno)."
                                              : "Povezivanje na Wi-Fi mrežu…";
  if (!s_sys.timeSynced) return "Sinhronizacija vremena…";
  if (s_sys.tuya.state == LinkState::Disabled && s_sys.ewelink.state == LinkState::Disabled)
    return "Nijedan nalog nije podešen.\nOtvori podešavanja (zupčanik gore desno).";
  String err;
  if (s_sys.tuya.state == LinkState::Error) err += s_sys.tuya.message + "\n";
  if (s_sys.ewelink.state == LinkState::Error) err += s_sys.ewelink.message + "\n";
  const bool loading = s_sys.tuya.state == LinkState::Connecting || s_sys.ewelink.state == LinkState::Connecting;
  if (loading) return "Učitavanje uređaja…\n" + err;
  if (err.length()) return err;
  return "Nema pronađenih prekidača.\nProveri TUYA_CATEGORIES u config.h.";
}

// Naslov sekcije u mreži: cela širina, pa naredne pločice idu u novi red
static void sectionHeader(const char* title, size_t count) {
  lv_obj_t* l = lv_label_create(s_grid);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_font(l, F_BODY, 0);
  lv_obj_set_style_text_color(l, C_MUTED, 0);
  lv_obj_set_style_pad_top(l, 6, 0);
  lv_label_set_text_fmt(l, "%s  ·  %u", title, (unsigned)count);
}

template <typename T>
static void dropHidden(std::vector<T>& v) {
  v.erase(std::remove_if(v.begin(), v.end(), [](const T& x) { return settings::isHidden(x.key()); }), v.end());
}

static String s_listSig;  // sekcije i ključevi napravljene mreže; nova mreža samo kad se promene

static void sync() {
  s_sys = g_store.systemStatus();
  std::vector<SwitchState> all = g_store.snapshot();
  const size_t total = all.size();
  dropHidden(all);
  std::vector<AlarmState> alarms = g_store.alarms();
  dropHidden(alarms);
  std::vector<SensorInfo> sensors = g_store.sensors();
  dropHidden(sensors);
  std::vector<CoverState> covers = g_store.covers();
  dropHidden(covers);
  std::vector<SmokeInfo> smokes = g_store.smokes();
  dropHidden(smokes);
  std::vector<ThermostatState> thermos = g_store.thermostats();
  dropHidden(thermos);
  std::vector<BinarySensorState> bins = g_store.binaries();
  dropHidden(bins);
  std::vector<scenes::Scene> scs = scenes::all();
  scs.erase(std::remove_if(scs.begin(), scs.end(),
                           [](const scenes::Scene& s) { return settings::isHidden(scenes::keyFor(s.id)); }),
            scs.end());

  // Prekidači po grupama (u grupi po imenu, kao u store-u)
  static const DeviceGroup BEFORE_COVERS[] = {DeviceGroup::Lights, DeviceGroup::Switches, DeviceGroup::Sockets};
  static const DeviceGroup AFTER_COVERS[] = {DeviceGroup::Climate, DeviceGroup::Security, DeviceGroup::Other};
  auto ofGroup = [&](DeviceGroup g) {
    std::vector<SwitchState> v;
    for (const SwitchState& s : all)
      if (s.group == g) v.push_back(s);
    return v;
  };

  // Redosled sekcija: Alarm, Temperatura, Svetla, Prekidači, Utičnice, Roletne, Klima, Sigurnost, Ostalo
  std::vector<SwitchState> items;  // prekidači redom kojim su u mreži (indeks = s_tiles)
  String sig;
  for (const AlarmState& a : alarms) sig += "a" + a.deviceId + ";";
  for (const scenes::Scene& s : scs) sig += "x" + s.id + ";";
  for (const SensorInfo& x : sensors) sig += "s" + x.deviceId + ";";
  for (DeviceGroup g : BEFORE_COVERS)
    for (const SwitchState& s : ofGroup(g)) {
      sig += String((int)g) + s.key() + ";";
      items.push_back(s);
    }
  for (const CoverState& c : covers) sig += "c" + c.key() + ";";
  for (const SmokeInfo& x : smokes) sig += "d" + x.deviceId + ";";
  for (const ThermostatState& x : thermos) sig += "t" + x.key() + ";";
  for (const BinarySensorState& x : bins) sig += "v" + x.key() + ";";
  for (DeviceGroup g : AFTER_COVERS)
    for (const SwitchState& s : ofGroup(g)) {
      sig += String((int)g) + s.key() + ";";
      items.push_back(s);
    }

  if (sig != s_listSig) {
    s_listSig = sig;
    lv_obj_clean(s_grid);
    s_tiles.clear();
    s_alarmTiles.clear();
    s_sensorTiles.clear();
    s_coverTiles.clear();
    s_smokeTiles.clear();
    s_thermoTiles.clear();
    s_binaryTiles.clear();
    s_sceneTiles.clear();
    s_tiles.reserve(items.size());
    if (!alarms.empty()) sectionHeader("Alarm", alarms.size());
    for (const AlarmState& a : alarms) s_alarmTiles.push_back(makeAlarmTile(a));
    if (!scs.empty()) sectionHeader("Scene", scs.size());
    for (const scenes::Scene& s : scs) s_sceneTiles.push_back(makeSceneTile(s));
    if (!sensors.empty()) sectionHeader("Temperatura", sensors.size());
    for (const SensorInfo& x : sensors) s_sensorTiles.push_back(makeSensorTile(x));
    // Sekcija prekidača jedne grupe; u "Klima" prvo idu termostati, u "Sigurnost" detektori dima
    // i senzori vrata / pokreta
    auto switchSection = [&](DeviceGroup g) {
      const size_t extra = g == DeviceGroup::Security ? smokes.size() + bins.size()
                           : g == DeviceGroup::Climate ? thermos.size()
                                                       : 0;
      size_t n = extra;
      for (const SwitchState& s : items) n += s.group == g;
      if (!n) return;
      sectionHeader(groupName(g), n);
      if (g == DeviceGroup::Climate)
        for (const ThermostatState& x : thermos) s_thermoTiles.push_back(makeThermoTile(x));
      if (g == DeviceGroup::Security) {
        for (const SmokeInfo& x : smokes) s_smokeTiles.push_back(makeSmokeTile(x));
        for (const BinarySensorState& x : bins) s_binaryTiles.push_back(makeBinaryTile(x));
      }
      for (const SwitchState& s : items)
        if (s.group == g) s_tiles.push_back(makeTile(s_tiles.size(), s));
    };
    for (DeviceGroup g : BEFORE_COVERS) switchSection(g);
    if (!covers.empty()) sectionHeader("Roletne", covers.size());
    for (const CoverState& c : covers) s_coverTiles.push_back(makeCoverTile(c));
    for (DeviceGroup g : AFTER_COVERS) switchSection(g);
  }
  for (size_t i = 0; i < alarms.size(); i++) applyAlarmTile(s_alarmTiles[i], alarms[i]);
  for (size_t i = 0; i < sensors.size(); i++) applySensorTile(s_sensorTiles[i], sensors[i]);
  for (size_t i = 0; i < covers.size(); i++) applyCoverTile(s_coverTiles[i], covers[i]);
  for (size_t i = 0; i < smokes.size(); i++) applySmokeTile(s_smokeTiles[i], smokes[i]);
  for (size_t i = 0; i < thermos.size(); i++) applyThermoTile(s_thermoTiles[i], thermos[i]);
  for (size_t i = 0; i < bins.size(); i++) applyBinaryTile(s_binaryTiles[i], bins[i]);
  for (size_t i = 0; i < scs.size(); i++) applySceneTile(s_sceneTiles[i], scs[i]);
  int onCount = 0, lighting = 0;
  for (size_t i = 0; i < items.size(); i++) {
    applyTile(s_tiles[i], items[i]);
    if (!groupIsLighting(items[i].group)) continue;
    lighting++;
    if (items[i].on && items[i].online) onCount++;
  }

  const String msg = alarms.empty() && sensors.empty() && covers.empty() && smokes.empty() && thermos.empty() &&
                             bins.empty()
                        ? statusMessage(items, total - items.size())
                        : String();
  if (msg.length()) setText(s_message, msg.c_str());
  setVisible(s_message, msg.length() > 0);
  setVisible(s_allOff, lighting > 0);

  char sub[48] = "";
  if (lighting) snprintf(sub, sizeof(sub), "Upaljeno: %d/%d", onCount, lighting);
  setText(s_subtitle, sub);
  // Kao na početnom ekranu: ikonica samo kad Wi-Fi ne radi (mesto u zaglavlju je tesno sa oba clouda)
  setVisible(s_wifi, !s_sys.wifiConnected);
  setTextColor(s_wifi, s_sys.wifiConnected ? C_TEXT : C_ERR);
  setChip(0, s_sys.tuya);
  setChip(1, s_sys.ewelink);

  if (s_sys.toastSeq != s_seenToast) {
    s_seenToast = s_sys.toastSeq;
    if (s_sys.toast.length()) showToast(s_sys.toast);
  }
}

static void updateClock(lv_timer_t*) {
  time_t now = time(nullptr);
  if (now < 1700000000) return;
  struct tm tmv;
  localtime_r(&now, &tmv);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  setText(s_clock, buf);  // timer ide svake sekunde, ali prekrtava samo kad se promeni minut
}

// ---------------------------------------------------------------- javno

void begin(lv_display_t* disp) {
  const uint32_t t0 = millis();
  theme::init(disp);

  s_scr = lv_screen_active();
  lv_obj_set_style_bg_color(s_scr, C_BG, 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  buildHeader();
  buildBody();
  lv_timer_create(updateClock, 1000, nullptr);
  sync();
  s_home = home_ui::build();
  lv_screen_load(s_home);
  settings_ui::build(s_home);
  alarm_ui::build(s_home);
  pin_prompt::build();
  cover_ui::build();
  thermo_ui::build();
  smoke_alert::build();  // posle ostalih prozorčića, da bude iznad njih
  // Prvo pokretanje: bez Wi-Fi mreže odmah otvori podešavanja
  if (settings::get().wifiSsid.isEmpty()) settings_ui::open(0);
  Serial.printf("[ui] ekrani napravljeni za %u ms\n", (unsigned)(millis() - t0));
}

void update() {
  const uint32_t rev = g_store.revision();
  const uint32_t hid = settings::hiddenRevision();
  if (rev != s_seenRevision || hid != s_seenHidden || scenes::revision() != s_seenScenes) {
    s_seenScenes = scenes::revision();
    s_seenRevision = rev;
    s_seenHidden = hid;
    sync();
  }
  settings_ui::update();
  alarm_ui::update();
  home_ui::update();
  pin_prompt::update();
  cover_ui::update();
  thermo_ui::update();
  smoke_alert::update();
  const String run = scenes::takeRunRequest();  // pokrenuto sa telefona
  if (run.length()) runScene(run);
  // Lista se sama vraća na početni ekran posle minuta bez dodira
  if (lv_screen_active() == s_scr && lv_display_get_inactive_time(nullptr) > 60000) lv_screen_load(s_home);
  // Odbrojavanje izlaznog kašnjenja na pločici (store se ne menja svake sekunde)
  static uint32_t lastAlarmTick = 0;
  if (!s_alarmTiles.empty() && millis() - lastAlarmTick >= 500) {
    lastAlarmTick = millis();
    std::vector<AlarmState> alarms = g_store.alarms();
    for (AlarmTile& t : s_alarmTiles) {
      for (const AlarmState& a : alarms) {
        if (a.deviceId == t.id) applyAlarmTile(t, a);
      }
    }
  }
}

lv_obj_t* mainScreen() { return s_home; }

void openList() { lv_screen_load(s_scr); }
void scrollList(int32_t y) { lv_obj_scroll_to_y(s_grid, y, LV_ANIM_OFF); }

void goHome() {
  home_ui::setEditMode(false);
  home_ui::setPage(0);  // posle gašenja ekrana uvek prvi deo
  cover_ui::close();
  thermo_ui::close();
  if (settings_ui::isOpen()) settings_ui::close();
  if (alarm_ui::isOpen()) alarm_ui::close();
  if (lv_screen_active() != s_home) lv_screen_load(s_home);
}

}  // namespace ui
