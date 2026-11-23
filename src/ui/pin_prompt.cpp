#include "ui/pin_prompt.h"

#include "config.h"
#include "model/settings.h"
#include "ui/theme.h"
#include "util/i18n.h"

namespace pin_prompt {

using namespace theme;

static constexpr size_t PIN_MIN = 4, PIN_MAX = 8;

static lv_obj_t *s_scr, *s_prev, *s_title, *s_dots, *s_msg;
static String s_pin;
static std::function<void()> s_onOk;
static bool s_showedLock = false;
static uint32_t s_lastTick = 0;

static void setText(lv_obj_t* l, const String& t) {
  if (strcmp(lv_label_get_text(l), t.c_str()) != 0) lv_label_set_text(l, t.c_str());
}

static void showDots() {
  if (s_pin.isEmpty()) {
    setText(s_dots, TR("Unesi PIN", "Enter PIN"));
    lv_obj_set_style_text_color(s_dots, C_MUTED, 0);
    return;
  }
  String d;
  for (size_t i = 0; i < s_pin.length(); i++) d += i ? " •" : "•";
  setText(s_dots, d);
  lv_obj_set_style_text_color(s_dots, C_TEXT, 0);
}

static void cancel() {
  s_pin = "";
  s_onOk = nullptr;
  lv_screen_load(s_prev ? s_prev : lv_screen_active());
}

static void submit() {
  if (s_pin.length() < PIN_MIN) {
    setText(s_msg, TR("PIN ima najmanje 4 cifre", "The PIN has at least 4 digits"));
    return;
  }
  const settings::PinResult r = settings::verifyAlarmPin(s_pin);
  s_pin = "";
  showDots();
  if (r == settings::PinResult::Ok) {
    setText(s_msg, "");
    auto cb = s_onOk;
    s_onOk = nullptr;
    if (cb) cb();  // učita zaštićeni ekran
  } else if (r == settings::PinResult::Wrong) {
    setText(s_msg, TR("Pogrešan PIN", "Wrong PIN"));
  }
  // Locked: poruku ispisuje update()
}

static void onPad(lv_event_t* e) {
  lv_obj_t* pad = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const char* txt = lv_buttonmatrix_get_button_text(pad, lv_buttonmatrix_get_selected_button(pad));
  if (!txt || settings::alarmPinLockedMs()) return;
  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
    if (s_pin.length()) s_pin.remove(s_pin.length() - 1);
  } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    submit();
    return;
  } else if (s_pin.length() < PIN_MAX) {
    s_pin += txt;
  }
  setText(s_msg, "");
  showDots();
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

void build() {
  s_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr, C_BG, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_button_create(s_scr);
  lv_obj_set_size(back, 130, 44);
  lv_obj_set_pos(back, 12, 8);
  lv_obj_set_style_radius(back, 22, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_style_bg_color(back, C_CARD, 0);
  lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(back, 6, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*) { cancel(); }, LV_EVENT_CLICKED, nullptr);
  label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_TEXT);
  label(back, TR("Otkaži", "Cancel"), F_BODY, C_TEXT);
  s_title = label(s_scr, "", F_NAME, C_TEXT);
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 16);

  // Levo: objašnjenje; desno: tastatura
  lv_obj_t* info = label(s_scr,
                         TR("Unesi PIN alarma.\nIsti PIN se koristi za isključivanje alarma.",
                            "Enter the alarm PIN.\nThe same PIN is used to disarm the alarm."),
                         F_BODY, C_MUTED);
  lv_obj_set_width(info, 340);
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  lv_obj_align(info, LV_ALIGN_LEFT_MID, 40, -40);
  s_dots = label(s_scr, "", F_TITLE, C_MUTED);
  lv_obj_align(s_dots, LV_ALIGN_LEFT_MID, 40, 40);
  s_msg = label(s_scr, "", F_SMALL, lv_color_hex(0xFFB4AB));
  lv_obj_set_width(s_msg, 340);
  lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_msg, LV_ALIGN_LEFT_MID, 40, 90);

  lv_obj_t* pad = lv_buttonmatrix_create(s_scr);
  static const char* map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                              LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
  lv_buttonmatrix_set_map(pad, map);
  lv_buttonmatrix_set_button_ctrl_all(pad, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
  lv_buttonmatrix_set_button_ctrl(pad, 11, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(pad, 340, 340);
  lv_obj_align(pad, LV_ALIGN_RIGHT_MID, -30, 28);
  lv_obj_set_style_text_font(pad, &lv_font_montserrat_28, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pad, 0, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_pad_row(pad, 10, 0);
  lv_obj_set_style_pad_column(pad, 10, 0);
  lv_obj_set_style_bg_color(pad, C_CARD, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(pad, lv_color_hex(0x2F3843), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(pad, C_ON, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(pad, C_TEXT, LV_PART_ITEMS);
  lv_obj_set_style_text_color(pad, C_ON_TEXT, (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(pad, 16, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(pad, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(pad, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(pad, onPad, LV_EVENT_VALUE_CHANGED, nullptr);
}

void ask(const String& title, std::function<void()> onOk) {
  if (!isOpen()) s_prev = lv_screen_active();
  s_onOk = std::move(onOk);
  s_pin = "";
  setText(s_title, title);
  setText(s_msg, "");
  showDots();
  lv_screen_load(s_scr);
}

bool isOpen() { return s_scr && lv_screen_active() == s_scr; }

void update() {
  if (!isOpen() || millis() - s_lastTick < 250) return;
  s_lastTick = millis();
  const uint32_t ms = settings::alarmPinLockedMs();
  if (ms) {
    setText(s_msg, TR("Previše pogrešnih pokušaja. Sačekaj ", "Too many wrong attempts. Wait ") + String((ms + 999) / 1000) + " s.");
    s_showedLock = true;
  } else if (s_showedLock) {
    s_showedLock = false;
    setText(s_msg, "");
  }
}

}  // namespace pin_prompt
