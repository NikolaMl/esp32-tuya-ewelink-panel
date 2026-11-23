#include "ui/cover_popup.h"

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "util/i18n.h"

namespace cover_ui {

using namespace theme;

static constexpr int CARD_W = 580, CARD_H = 330, BTN_W = 164, BTN_H = 112;
static constexpr uint32_t AUTO_CLOSE_MS = 30000;

static lv_obj_t *s_scrim, *s_name, *s_state, *s_bar;
static lv_obj_t* s_openedOn = nullptr;  // ekran ispod; ako se promeni, prozorče se zatvara
static String s_key;
static uint32_t s_seenRev = 0;

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

void command(const String& key, CoverAction a) {
  for (const CoverState& c : g_store.covers()) {
    if (c.key() != key) continue;
    if (!c.online) ui::showToast(c.name + TR(" je trenutno nedostupna", " is offline right now"));
    else if (!cloud::requestCover(key, a) && !c.pending)
      ui::showToast(TR("Komanda nije poslata, pokušaj ponovo", "Command not sent, try again"));
    return;
  }
}

static void onAction(lv_event_t* e) {
  command(s_key, static_cast<CoverAction>((uintptr_t)lv_event_get_user_data(e)));
}

static void actionButton(lv_obj_t* parent, const char* symbol, const char* text, CoverAction a) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, BTN_W, BTN_H);
  lv_obj_set_style_radius(b, 22, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x262E38), 0);
  lv_obj_set_style_bg_color(b, C_ON, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(b, C_TEXT, 0);
  lv_obj_set_style_text_color(b, C_ON_TEXT, LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b, 4, 0);
  lv_obj_add_event_cb(b, onAction, LV_EVENT_CLICKED, (void*)(uintptr_t)a);
  // Ikonica i tekst u posebnim labelama (simboli ne postoje u TTF fontu); boju nasleđuju od dugmeta
  lv_obj_t* i = lv_label_create(b);
  lv_obj_set_style_text_font(i, &lv_font_montserrat_40, 0);
  lv_label_set_text(i, symbol);
  lv_obj_t* t = lv_label_create(b);
  lv_obj_set_style_text_font(t, F_BODY, 0);
  lv_label_set_text(t, text);
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

  // Položaj (samo ako ga modul javlja)
  s_bar = lv_bar_create(card);
  lv_obj_set_size(s_bar, CARD_W - 48, 8);
  lv_obj_align(s_bar, LV_ALIGN_TOP_LEFT, 0, 70);
  lv_bar_set_range(s_bar, 0, 100);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x262E38), 0);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_bar, C_ON, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bar, 4, 0);
  lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, CARD_W - 48, BTN_H);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  actionButton(row, LV_SYMBOL_UP, TR("Gore", "Up"), CoverAction::Open);
  actionButton(row, LV_SYMBOL_STOP, "Stop", CoverAction::Stop);
  actionButton(row, LV_SYMBOL_DOWN, TR("Dole", "Down"), CoverAction::Close);

  lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
}

static void refresh() {
  if (settings::isHidden(s_key)) {
    close();
    return;
  }
  for (const CoverState& c : g_store.covers()) {
    if (c.key() != s_key) continue;
    setText(s_name, c.name.c_str());
    String st;
    if (!c.online) st = TR("Nedostupna", "Offline");
    else if (c.pending) st = TR("Šaljem…", "Sending…");
    else if (c.position >= 0) st = TR("Položaj ", "Position ") + String(c.position) + " %";
    else st = TR("Modul ne javlja položaj", "No position reported");
    setText(s_state, st.c_str());
    if (c.position >= 0) {
      if (lv_obj_has_flag(s_bar, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      if (lv_bar_get_value(s_bar) != c.position) lv_bar_set_value(s_bar, c.position, LV_ANIM_OFF);
    } else {
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  close();  // roletna je nestala sa naloga
}

void open(const String& key) {
  s_key = key;
  s_openedOn = lv_screen_active();
  s_seenRev = g_store.revision();
  lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_scrim);
  refresh();
  cloud::requestRefresh();  // sveže stanje (položaj) odmah
}

void close() {
  if (s_scrim) lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
  s_key = "";
}

bool isOpen() { return s_scrim && !lv_obj_has_flag(s_scrim, LV_OBJ_FLAG_HIDDEN); }

void update() {
  if (!isOpen()) return;
  if (settings::isHidden(s_key)) {  // isključen u aplikaciji dok je prozorče otvoreno
    close();
    return;
  }
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

}  // namespace cover_ui
