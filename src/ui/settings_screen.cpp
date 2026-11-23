#include "ui/settings_screen.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "net/ewelink_client.h"
#include "net/web_portal.h"
#include "ui/pin_prompt.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "util/i18n.h"
#include "util/restart.h"

namespace settings_ui {

using namespace theme;

static constexpr int TOP_H = 60;
static constexpr int KB_H = 220;

static lv_obj_t* s_main = nullptr;
static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_tv = nullptr;
static lv_obj_t* s_kb = nullptr;

// Wi-Fi
static lv_obj_t *s_wifiStatus, *s_wifiList, *s_scanLbl, *s_ssidTa, *s_passTa, *s_showPass;
// Tuya
static lv_obj_t *s_tuyaDd, *s_tuyaId, *s_tuyaSecret, *s_tuyaStatus, *s_tuyaHint;
// eWeLink
static lv_obj_t *s_ewId, *s_ewSecret, *s_ewStatus, *s_ewQr, *s_ewQrHint, *s_ewRedirect, *s_ewResult;
// Prekidači
static lv_obj_t* s_swList;
// Alarm
static lv_obj_t *s_pinCur, *s_pinCurLbl, *s_pinNew, *s_pinNew2, *s_pinStatus;
static uint32_t s_seenLayout = 0;
// Web
static lv_obj_t *s_webQr, *s_webUrl, *s_webPin, *s_webInfo;
static lv_obj_t* s_langBtn[2];  // [0] = English, [1] = Srpski

static uint32_t s_seenScan = 0, s_seenStore = 0, s_seenOauth = 0, s_seenSettings = 0;
static String s_lastIp;

// ---------------------------------------------------------------- pomoćne

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* column(lv_obj_t* parent, int32_t w) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 8, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, bool primary) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 48);
  lv_obj_set_style_radius(b, 24, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 22, 0);
  lv_obj_set_style_bg_color(b, primary ? C_ON : C_CARD, 0);
  lv_obj_set_style_border_width(b, primary ? 0 : 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0x333C47), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = label(b, text, F_BODY, primary ? C_ON_TEXT : C_TEXT);
  lv_obj_center(l);
  return b;
}

static void hideKeyboard() {
  if (lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) return;
  lv_obj_t* ta = lv_keyboard_get_textarea(s_kb);
  if (ta) lv_obj_remove_state(ta, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(s_tv, LCD_V_RES - TOP_H);
}

static void showKeyboard(lv_obj_t* ta) {
  // PIN polja (dozvoljene samo cifre) dobijaju numeričku tastaturu
  lv_keyboard_set_mode(s_kb, lv_textarea_get_accepted_chars(ta) ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(s_kb, ta);
  lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(s_tv, LCD_V_RES - TOP_H - KB_H);
  lv_obj_update_layout(s_scr);
  lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

static void onTaEvent(lv_event_t* e) {
  lv_obj_t* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) showKeyboard(ta);
}

static lv_obj_t* field(lv_obj_t* parent, const char* title, bool password, int32_t w) {
  label(parent, title, F_SMALL, C_MUTED);
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, w);
  lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(ta, C_INPUT, 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(0x333C47), 0);
  lv_obj_set_style_border_color(ta, C_ON, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(ta, onTaEvent, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, onTaEvent, LV_EVENT_CLICKED, nullptr);
  return ta;
}

static String taText(lv_obj_t* ta) {
  String s = lv_textarea_get_text(ta);
  s.trim();
  return s;
}

// Menja samo ako je drugačije: svako prekrtavanje opterećuje PSRAM (smetnje na ekranu)
static void setText(lv_obj_t* l, const String& text) {
  if (strcmp(lv_label_get_text(l), text.c_str()) != 0) lv_label_set_text(l, text.c_str());
}

static void setStatus(lv_obj_t* l, const String& text, lv_color_t color) {
  setText(l, text);
  if (!lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), color)) lv_obj_set_style_text_color(l, color, 0);
}

static void cloudStatusText(lv_obj_t* l, const CloudStatus& cs, const char* name) {
  switch (cs.state) {
    case LinkState::Ok: setStatus(l, String(name) + TR(": povezano", ": connected"), C_OK); break;
    case LinkState::Connecting: setStatus(l, String(name) + TR(": povezivanje…", ": connecting…"), C_WARN); break;
    case LinkState::Disabled: setStatus(l, String(name) + ": " + cs.message, C_MUTED); break;
    case LinkState::Error: setStatus(l, cs.message, C_ERR); break;
  }
}

// ---------------------------------------------------------------- Wi-Fi tab

static void onScan(lv_event_t*) {
  cloud::requestWifiScan();
  lv_label_set_text(s_scanLbl, TR("Skeniram…", "Scanning…"));
}

static void onNetworkPicked(lv_event_t* e) {
  lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const char* ssid = static_cast<const char*>(lv_event_get_user_data(e));
  (void)btn;
  lv_textarea_set_text(s_ssidTa, ssid);
  lv_textarea_set_text(s_passTa, "");
  lv_obj_add_state(s_passTa, LV_STATE_FOCUSED);
  showKeyboard(s_passTa);
}

static void onShowPass(lv_event_t*) {
  lv_textarea_set_password_mode(s_passTa, !lv_obj_has_state(s_showPass, LV_STATE_CHECKED));
}

static void onWifiSave(lv_event_t*) {
  hideKeyboard();
  const String ssid = taText(s_ssidTa);
  if (ssid.isEmpty()) {
    setStatus(s_wifiStatus, TR("Unesi naziv mreže (SSID)", "Enter the network name (SSID)"), C_ERR);
    return;
  }
  settings::setWifi(ssid, String(lv_textarea_get_text(s_passTa)));
  setStatus(s_wifiStatus, TR("Povezujem se na ", "Connecting to ") + ssid + "…", C_WARN);
}

static void fillScanList() {
  lv_obj_clean(s_wifiList);
  const auto nets = cloud::scanResults();
  if (nets.empty()) {
    lv_list_add_text(s_wifiList, TR("Nema pronađenih mreža", "No networks found"));
    return;
  }
  for (const auto& n : nets) {
    String txt = n.ssid + "   " + String(n.rssi) + " dBm" + (n.secure ? "" : TR("  (otvorena)", "  (open)"));
    lv_obj_t* b = lv_list_add_button(s_wifiList, nullptr, txt.c_str());
    lv_obj_set_style_text_font(b, F_SMALL, 0);
    // user_data: kopija SSID-a, oslobađa se kad se dugme obriše
    char* ssid = strdup(n.ssid.c_str());
    lv_obj_add_event_cb(b, onNetworkPicked, LV_EVENT_CLICKED, ssid);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ssid);
  }
}

static void buildWifiTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tab, 20, 0);

  lv_obj_t* left = column(tab, 330);
  lv_obj_t* row = lv_obj_create(left);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(row, TR("Dostupne mreže", "Networks"), F_BODY, C_TEXT);
  lv_obj_t* scan = button(row, TR("Skeniraj", "Scan"), onScan, false);
  s_scanLbl = lv_obj_get_child(scan, 0);

  s_wifiList = lv_list_create(left);
  lv_obj_set_size(s_wifiList, LV_PCT(100), 250);
  lv_obj_set_style_bg_color(s_wifiList, C_CARD, 0);
  lv_obj_set_style_border_width(s_wifiList, 0, 0);
  lv_obj_set_style_text_font(s_wifiList, F_SMALL, 0);
  lv_list_add_text(s_wifiList, TR("Pritisni „Skeniraj“", "Tap \"Scan\""));

  lv_obj_t* right = column(tab, 380);
  s_wifiStatus = label(right, "", F_BODY, C_MUTED);
  lv_obj_set_width(s_wifiStatus, LV_PCT(100));
  lv_label_set_long_mode(s_wifiStatus, LV_LABEL_LONG_WRAP);
  s_ssidTa = field(right, TR("Naziv mreže (SSID)", "Network name (SSID)"), false, LV_PCT(100));
  s_passTa = field(right, TR("Lozinka", "Password"), true, LV_PCT(100));
  s_showPass = lv_checkbox_create(right);
  lv_checkbox_set_text(s_showPass, TR("Prikaži lozinku", "Show password"));
  lv_obj_set_style_text_font(s_showPass, F_SMALL, 0);
  lv_obj_set_style_text_font(s_showPass, &lv_font_montserrat_16, LV_PART_INDICATOR);  // kvačica = LVGL simbol
  lv_obj_set_style_text_color(s_showPass, C_TEXT, 0);
  lv_obj_add_event_cb(s_showPass, onShowPass, LV_EVENT_VALUE_CHANGED, nullptr);
  button(right, TR("Sačuvaj i poveži", "Save and connect"), onWifiSave, true);
}

// ---------------------------------------------------------------- Tuya tab

static void onTuyaSave(lv_event_t*) {
  hideKeyboard();
  const uint32_t sel = lv_dropdown_get_selected(s_tuyaDd);
  const String id = taText(s_tuyaId), secret = taText(s_tuyaSecret);
  if (id.isEmpty() != secret.isEmpty()) {
    setStatus(s_tuyaStatus, TR("Unesi i Access ID i Access Secret", "Enter both Access ID and Access Secret"), C_ERR);
    return;
  }
  settings::setTuya(TUYA_REGIONS[sel < TUYA_REGION_COUNT ? sel : 0].endpoint, id, secret);
  setStatus(s_tuyaStatus, id.isEmpty() ? TR("Tuya je isključen", "Tuya is turned off") : TR("Sačuvano, povezujem…", "Saved, connecting…"),
            C_WARN);
}

static void buildTuyaTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* info = label(tab,
                         TR("Podaci iz iot.tuya.com → Cloud → tvoj projekat → Overview.\n"
                            "Smart Life nalog mora biti povezan sa projektom (Devices → Link App Account).",
                            "From iot.tuya.com → Cloud → your project → Overview.\n"
                            "Your Smart Life account must be linked to the project (Devices → Link App Account)."),
                         F_SMALL, C_MUTED);
  lv_obj_set_width(info, LV_PCT(100));
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

  label(tab, TR("Data centar", "Data center"), F_SMALL, C_MUTED);
  s_tuyaDd = lv_dropdown_create(tab);
  String opts;
  for (size_t i = 0; i < TUYA_REGION_COUNT; i++) {
    if (i) opts += "\n";
    opts += TUYA_REGIONS[i].label;
  }
  lv_dropdown_set_options(s_tuyaDd, opts.c_str());
  lv_obj_set_width(s_tuyaDd, 360);
  lv_obj_set_style_text_font(s_tuyaDd, &lv_font_montserrat_20, 0);

  s_tuyaId = field(tab, "Access ID / Client ID", false, 520);
  s_tuyaSecret = field(tab, "Access Secret / Client Secret", true, 520);

  lv_obj_t* row = lv_obj_create(tab);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 16, 0);
  lv_obj_set_style_pad_top(row, 8, 0);
  button(row, TR("Sačuvaj", "Save"), onTuyaSave, true);
  s_tuyaStatus = label(row, "", F_SMALL, C_MUTED);
  lv_obj_set_flex_grow(s_tuyaStatus, 1);
  lv_label_set_long_mode(s_tuyaStatus, LV_LABEL_LONG_WRAP);

  s_tuyaHint = label(tab, "", F_SMALL, C_MUTED);
}

// ---------------------------------------------------------------- eWeLink tab

static void onEwSave(lv_event_t*) {
  hideKeyboard();
  const String id = taText(s_ewId), secret = taText(s_ewSecret);
  if (id.isEmpty() != secret.isEmpty()) {
    setStatus(s_ewStatus, TR("Unesi i App ID i App Secret", "Enter both App ID and App Secret"), C_ERR);
    return;
  }
  settings::setEWeLink(id, secret);
  setStatus(s_ewStatus, TR("Sačuvano. Sada poveži nalog →", "Saved. Now link your account →"), C_WARN);
}

static void onEwConnect(lv_event_t*) {
  hideKeyboard();
  const Settings st = settings::get();
  if (st.ewAppId.isEmpty() || st.ewAppSecret.isEmpty()) {
    setStatus(s_ewResult, TR("Prvo unesi i sačuvaj App ID i App Secret", "First enter and save App ID and App Secret"), C_ERR);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStatus(s_ewResult, TR("Panel nije povezan na Wi-Fi", "The panel is not on Wi-Fi"), C_ERR);
    return;
  }
  const String url = portal::newLoginUrl();
  lv_qrcode_update(s_ewQr, url.c_str(), url.length());
  lv_obj_remove_flag(s_ewQr, LV_OBJ_FLAG_HIDDEN);
  setStatus(s_ewResult,
            TR("Skeniraj QR kod telefonom (isti Wi-Fi) i prijavi se eWeLink nalogom. Link važi 10 min.",
               "Scan the QR code with your phone (same Wi-Fi) and sign in with your eWeLink account. The link is valid for 10 min."),
            C_TEXT);
  Serial.printf("[ui] eWeLink prijava: %s\n", url.c_str());
}

static void onEwLogout(lv_event_t*) {
  EWeLinkClient::clearTokens();
  settings::touchEWeLinkTokens();
  setStatus(s_ewResult, TR("Odjavljeno sa eWeLink naloga", "Signed out of eWeLink"), C_MUTED);
}

static void buildEWeLinkTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tab, 20, 0);

  lv_obj_t* left = column(tab, 400);
  s_ewId = field(left, "App ID (dev.ewelink.cc)", false, LV_PCT(100));
  s_ewSecret = field(left, "App Secret", true, LV_PCT(100));
  lv_obj_t* row = lv_obj_create(left);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_style_pad_top(row, 8, 0);
  button(row, TR("Sačuvaj", "Save"), onEwSave, true);
  button(row, TR("Odjavi", "Sign out"), onEwLogout, false);
  s_ewStatus = label(left, "", F_SMALL, C_MUTED);
  lv_obj_set_width(s_ewStatus, LV_PCT(100));
  lv_label_set_long_mode(s_ewStatus, LV_LABEL_LONG_WRAP);
  label(left, TR("Redirect URL za dev.ewelink.cc:", "Redirect URL for dev.ewelink.cc:"), F_SMALL, C_MUTED);
  s_ewRedirect = label(left, "", F_SMALL, C_TEXT);
  lv_obj_set_width(s_ewRedirect, LV_PCT(100));
  lv_label_set_long_mode(s_ewRedirect, LV_LABEL_LONG_WRAP);

  lv_obj_t* right = column(tab, 310);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  button(right, TR("Poveži nalog", "Link account"), onEwConnect, true);
  s_ewQr = lv_qrcode_create(right);
  lv_qrcode_set_size(s_ewQr, 170);
  lv_qrcode_set_dark_color(s_ewQr, lv_color_black());
  lv_qrcode_set_light_color(s_ewQr, lv_color_white());
  lv_obj_set_style_border_color(s_ewQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(s_ewQr, 6, 0);
  lv_obj_add_flag(s_ewQr, LV_OBJ_FLAG_HIDDEN);
  s_ewResult = label(right, "", F_SMALL, C_MUTED);
  lv_obj_set_width(s_ewResult, LV_PCT(100));
  lv_label_set_long_mode(s_ewResult, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_ewResult, LV_TEXT_ALIGN_CENTER, 0);
  s_ewQrHint = s_ewResult;
}

// ---------------------------------------------------------------- Web tab

// ---------------------------------------------------------------- Prekidači tab (prikaži/sakrij)

static void onSwitchVisibility(lv_event_t* e) {
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const char* key = static_cast<const char*>(lv_event_get_user_data(e));
  settings::setHidden(key, !lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void addVisibilityRow(const String& title, const String& sub, const String& key);

static void fillSwitchList() {
  s_seenLayout = g_store.layoutRevision();
  lv_obj_clean(s_swList);
  const std::vector<SwitchState> items = g_store.snapshot();
  if (items.empty() && g_store.alarms().empty() && g_store.sensors().empty() && g_store.covers().empty() &&
      g_store.smokes().empty() && g_store.thermostats().empty() && g_store.binaries().empty()) {
    label(s_swList, TR("Još nema uređaja. Poveži Tuya ili eWeLink nalog.", "No devices yet. Link a Tuya or eWeLink account."),
          F_BODY, C_MUTED);
    return;
  }
  const char* offF = TR(" · nedostupna", " · offline");  // ženski rod (centrala, roletna)
  const char* offM = TR(" · nedostupan", " · offline");
  for (const AlarmState& a : g_store.alarms()) {
    addVisibilityRow(a.name, String(TR("Alarm centrala · Tuya", "Alarm hub · Tuya")) + (a.online ? "" : offF), a.key());
  }
  for (const SensorInfo& x : g_store.sensors()) {
    addVisibilityRow(x.name, String(TR("Senzor temperature · Tuya", "Temperature sensor · Tuya")) + (x.online ? "" : offM),
                     x.key());
  }
  for (const SmokeInfo& s : g_store.smokes()) {
    addVisibilityRow(s.name, String(TR("Detektor dima · Tuya", "Smoke detector · Tuya")) + (s.online ? "" : offM), s.key());
  }
  for (const CoverState& c : g_store.covers()) {
    addVisibilityRow(c.name, String(TR("Roletna · Tuya", "Blind · Tuya")) + (c.online ? "" : offF), c.key());
  }
  for (const ThermostatState& t : g_store.thermostats()) {
    addVisibilityRow(t.name, String(TR("Termostat · Tuya", "Thermostat · Tuya")) + (t.online ? "" : offM), t.key());
  }
  for (const BinarySensorState& b : g_store.binaries()) {
    addVisibilityRow(b.name, String(binaryKindName(b.kind)) + " · Tuya" + (b.online ? "" : offM), b.key());
  }
  for (const SwitchState& it : items) {
    addVisibilityRow(it.name, String(groupName(it.group)) + " · " + cloudName(it.cloud) + (it.online ? "" : offM), it.key());
  }
}

static void addVisibilityRow(const String& title, const String& sub, const String& key) {
  lv_obj_t* row = lv_obj_create(s_swList);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 64);
  lv_obj_set_style_bg_color(row, C_CARD, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(row, 14, 0);
  lv_obj_set_style_pad_hor(row, 16, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* name = label(row, title.c_str(), F_BODY, C_TEXT);
  lv_obj_set_width(name, 560);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -10);
  lv_obj_t* subl = label(row, sub.c_str(), F_SMALL, C_MUTED);
  lv_obj_align(subl, LV_ALIGN_LEFT_MID, 0, 14);

  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 64, 34);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_ext_click_area(sw, 14);
  if (!settings::isHidden(key)) lv_obj_add_state(sw, LV_STATE_CHECKED);
  char* k = strdup(key.c_str());  // oslobađa se kad se red obriše
  lv_obj_add_event_cb(sw, onSwitchVisibility, LV_EVENT_VALUE_CHANGED, k);
  lv_obj_add_event_cb(sw, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, k);
}

static void buildSwitchesTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* hint = label(tab,
                         TR("Isključen uređaj panel potpuno ignoriše: nema ga u listi ni na slici, bez komandi i bez upozorenja.",
                            "The panel fully ignores a device that is turned off here: not in the list or on the photo, "
                            "no commands and no warnings."),
                         F_SMALL, C_MUTED);
  lv_obj_set_width(hint, LV_PCT(100));  // prelama se u dva reda umesto da bude odsečen desno
  s_swList = lv_obj_create(tab);
  lv_obj_remove_style_all(s_swList);
  lv_obj_set_size(s_swList, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_swList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_swList, 8, 0);
  lv_obj_remove_flag(s_swList, LV_OBJ_FLAG_SCROLLABLE);  // skroluje ceo tab
}

// ---------------------------------------------------------------- Alarm tab (PIN)

static void refreshPinForm() {
  const bool has = settings::hasAlarmPin();
  if (has) {
    lv_obj_remove_flag(s_pinCurLbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_pinCur, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_pinCurLbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pinCur, LV_OBJ_FLAG_HIDDEN);
  }
  lv_textarea_set_text(s_pinCur, "");
  lv_textarea_set_text(s_pinNew, "");
  lv_textarea_set_text(s_pinNew2, "");
}

static void onPinSave(lv_event_t*) {
  hideKeyboard();
  const String cur = taText(s_pinCur), n1 = taText(s_pinNew), n2 = taText(s_pinNew2);
  if (settings::hasAlarmPin()) {
    // Promena PIN-a traži stari PIN — inače bi bilo ko mogao da postavi svoj i isključi alarm
    const settings::PinResult r = settings::verifyAlarmPin(cur);
    if (r == settings::PinResult::Locked) {
      setStatus(s_pinStatus,
                TR("Previše pogrešnih pokušaja. Sačekaj ", "Too many wrong attempts. Wait ") +
                    String((settings::alarmPinLockedMs() + 999) / 1000) + " s.",
                C_ERR);
      return;
    }
    if (r == settings::PinResult::Wrong) {
      setStatus(s_pinStatus, TR("Trenutni PIN nije tačan", "The current PIN is wrong"), C_ERR);
      lv_textarea_set_text(s_pinCur, "");
      return;
    }
  }
  if (n1.length() < 4 || n1.length() > 8) {
    setStatus(s_pinStatus, TR("Novi PIN mora imati 4 do 8 cifara", "The new PIN must have 4 to 8 digits"), C_ERR);
    return;
  }
  if (n1 != n2) {
    setStatus(s_pinStatus, TR("Novi PIN i ponovljeni PIN se razlikuju", "The two new PINs don't match"), C_ERR);
    return;
  }
  settings::setAlarmPin(n1);
  refreshPinForm();
  setStatus(s_pinStatus, TR("PIN je sačuvan", "PIN saved"), C_OK);
}

static lv_obj_t* pinField(lv_obj_t* parent, const char* title, lv_obj_t** labelOut = nullptr) {
  const uint32_t before = lv_obj_get_child_count(parent);
  lv_obj_t* ta = field(parent, title, true, 260);
  if (labelOut) *labelOut = lv_obj_get_child(parent, before);  // naslov polja
  lv_textarea_set_accepted_chars(ta, "0123456789");
  lv_textarea_set_max_length(ta, 8);
  return ta;
}

static void buildAlarmTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* info = label(tab,
                         TR("PIN se traži za isključivanje alarma sa panela (uključivanje ne traži PIN). "
                            "Posle 5 pogrešnih pokušaja panel se privremeno zaključava.",
                            "The PIN is needed to disarm the alarm from the panel (arming needs no PIN). "
                            "After 5 wrong attempts the panel locks for a while."),
                         F_SMALL, C_MUTED);
  lv_obj_set_width(info, LV_PCT(100));
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  s_pinCur = pinField(tab, TR("Trenutni PIN", "Current PIN"), &s_pinCurLbl);
  s_pinNew = pinField(tab, TR("Novi PIN (4–8 cifara)", "New PIN (4–8 digits)"));
  s_pinNew2 = pinField(tab, TR("Ponovi novi PIN", "Repeat new PIN"));
  lv_obj_t* row = lv_obj_create(tab);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 16, 0);
  lv_obj_set_style_pad_top(row, 6, 0);
  button(row, TR("Sačuvaj PIN", "Save PIN"), onPinSave, true);
  s_pinStatus = label(row, "", F_SMALL, C_MUTED);
}

// Jezik: dugme izabranog jezika je istaknuto. Promena se čuva i panel se restartuje
// (ekrani se prave na jeziku pokretanja); web PIN ostaje isti posle restarta.
static void styleLangButtons() {
  const Lang cur = settings::language();
  for (int i = 0; i < 2; i++) {
    const bool sel = (i == 0) == (cur == Lang::En);
    lv_obj_set_style_bg_color(s_langBtn[i], sel ? C_ON : C_CARD, 0);
    lv_obj_set_style_border_width(s_langBtn[i], sel ? 0 : 1, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_langBtn[i], 0), sel ? C_ON_TEXT : C_TEXT, 0);
  }
}

static void onLangPicked(lv_event_t* e) {
  lv_obj_t* b = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const Lang l = b == s_langBtn[0] ? Lang::En : Lang::Sr;
  if (!settings::setLanguage(l)) return;
  styleLangButtons();
  // Poruka na izabranom jeziku (TR bi dao jezik ovog pokretanja)
  ui::showToast(l == Lang::En ? "Restarting panel…" : "Restartujem panel…", false);
  restart::soon(1500);
}

static void buildWebTab(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tab, 28, 0);
  s_webQr = lv_qrcode_create(tab);
  lv_qrcode_set_size(s_webQr, 200);
  lv_qrcode_set_dark_color(s_webQr, lv_color_black());
  lv_qrcode_set_light_color(s_webQr, lv_color_white());
  lv_obj_set_style_border_color(s_webQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(s_webQr, 6, 0);

  lv_obj_t* right = column(tab, 480);
  lv_obj_t* l = label(right,
                      TR("Skeniraj QR kod telefonom (isti Wi-Fi): raspored temperatura, uređaji, ekran, fotografija i nalozi.",
                         "Scan the QR code with your phone (same Wi-Fi): layout, devices, screen, photo and accounts."),
                      F_SMALL, C_MUTED);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  s_webUrl = label(right, "", F_NAME, C_ON);
  s_webPin = label(right, "", F_NAME, C_TEXT);
  s_webInfo = label(right, "", F_SMALL, C_MUTED);
  lv_obj_set_width(s_webInfo, LV_PCT(100));
  lv_label_set_long_mode(s_webInfo, LV_LABEL_LONG_WRAP);

  // Jezik / Language (naziv na oba jezika, da ga nađe i ko ne razume trenutni)
  lv_obj_t* lrow = lv_obj_create(right);
  lv_obj_remove_style_all(lrow);
  lv_obj_set_size(lrow, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(lrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(lrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(lrow, 12, 0);
  lv_obj_set_style_pad_top(lrow, 6, 0);
  lv_obj_remove_flag(lrow, LV_OBJ_FLAG_SCROLLABLE);
  label(lrow, "Jezik / Language", F_SMALL, C_MUTED);
  s_langBtn[0] = button(lrow, "English", onLangPicked, false);
  s_langBtn[1] = button(lrow, "Srpski", onLangPicked, false);
  styleLangButtons();
}

// ---------------------------------------------------------------- javno

static void loadFields() {
  const Settings st = settings::get();
  lv_textarea_set_text(s_ssidTa, st.wifiSsid.c_str());
  lv_textarea_set_text(s_passTa, st.wifiPass.c_str());
  for (size_t i = 0; i < TUYA_REGION_COUNT; i++) {
    if (st.tuyaEndpoint == TUYA_REGIONS[i].endpoint) lv_dropdown_set_selected(s_tuyaDd, i);
  }
  lv_textarea_set_text(s_tuyaId, st.tuyaId.c_str());
  lv_textarea_set_text(s_tuyaSecret, st.tuyaSecret.c_str());
  lv_textarea_set_text(s_ewId, st.ewAppId.c_str());
  lv_textarea_set_text(s_ewSecret, st.ewAppSecret.c_str());
}

static void refreshNetworkInfo(const SystemStatus& sys) {
  const String ip = sys.wifiConnected ? sys.ip : String();
  if (ip == s_lastIp && s_lastIp.length()) return;
  s_lastIp = ip;
  if (!sys.wifiConnected) {
    lv_label_set_text(s_webUrl, TR("Panel nije na Wi-Fi mreži", "The panel is not on Wi-Fi"));
    lv_label_set_text(s_webPin, "");
    lv_label_set_text(s_ewRedirect, TR("(prvo poveži Wi-Fi)", "(connect Wi-Fi first)"));
    lv_label_set_text(s_tuyaHint, "");
    lv_obj_add_flag(s_webQr, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const String url = "http://" + ip + "/";
  const String qr = url + "?pin=" + settings::webPin();
  lv_label_set_text(s_webUrl, url.c_str());
  lv_label_set_text_fmt(s_webPin, "PIN: %s", settings::webPin().c_str());
  lv_qrcode_update(s_webQr, qr.c_str(), qr.length());
  lv_obj_remove_flag(s_webQr, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_ewRedirect, ("http://" + ip + "/ewelink/callback").c_str());
  lv_label_set_text_fmt(s_tuyaHint, TR("Lakše: otvori %s u browseru (PIN %s)", "Easier: open %s in a browser (PIN %s)"),
                        url.c_str(), settings::webPin().c_str());
}

static void refresh() {
  const SystemStatus sys = g_store.systemStatus();
  const Settings st = settings::get();

  if (sys.wifiConnected)
    setStatus(s_wifiStatus, TR("Povezano: ", "Connected: ") + st.wifiSsid + TR("\nIP adresa: ", "\nIP address: ") + sys.ip, C_OK);
  else if (st.wifiSsid.length())
    setStatus(s_wifiStatus, TR("Povezivanje na ", "Connecting to ") + st.wifiSsid + "…", C_WARN);
  else setStatus(s_wifiStatus, TR("Wi-Fi nije podešen. Izaberi mrežu sa liste.", "Wi-Fi is not set up. Pick a network from the list."),
                 C_MUTED);

  cloudStatusText(s_tuyaStatus, sys.tuya, "Tuya");
  cloudStatusText(s_ewStatus, sys.ewelink, "eWeLink");
  refreshNetworkInfo(sys);

  if (lv_tabview_get_tab_active(s_tv) == TAB_WEB) {
    char info[96];
    snprintf(info, sizeof(info), TR("Uređaj: %s\nSlobodna memorija: %u KB RAM, %u KB PSRAM", "Device: %s\nFree memory: %u KB RAM, %u KB PSRAM"),
             WiFi.macAddress().c_str(),
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
    setText(s_webInfo, info);
  }
}

void build(lv_obj_t* mainScreen) {
  s_main = mainScreen;
  s_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr, C_BG, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  // Gornja traka
  lv_obj_t* top = lv_obj_create(s_scr);
  lv_obj_remove_style_all(top);
  lv_obj_set_size(top, LCD_H_RES, TOP_H);
  lv_obj_set_style_pad_hor(top, 12, 0);
  lv_obj_t* back = lv_button_create(top);
  lv_obj_set_size(back, 120, 44);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(back, 22, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_style_bg_color(back, C_CARD, 0);
  lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(back, 6, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
  label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_TEXT);
  label(back, TR("Nazad", "Back"), F_BODY, C_TEXT);
  lv_obj_t* title = label(top, TR("Podešavanja", "Settings"), F_NAME, C_TEXT);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // Tabovi
  s_tv = lv_tabview_create(s_scr);
  lv_obj_set_size(s_tv, LCD_H_RES, LCD_V_RES - TOP_H);
  lv_obj_set_pos(s_tv, 0, TOP_H);
  lv_tabview_set_tab_bar_size(s_tv, 50);
  lv_obj_set_style_bg_color(s_tv, C_BG, 0);
  lv_obj_t* bar = lv_tabview_get_tab_bar(s_tv);
  lv_obj_set_style_bg_color(bar, C_CARD, 0);
  lv_obj_set_style_text_font(bar, F_BODY, 0);
  lv_obj_t* content = lv_tabview_get_content(s_tv);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);  // tabovi se menjaju samo dodirom na naslov

  lv_obj_t* tWifi = lv_tabview_add_tab(s_tv, "Wi-Fi");
  lv_obj_t* tTuya = lv_tabview_add_tab(s_tv, "Tuya");
  lv_obj_t* tEw = lv_tabview_add_tab(s_tv, "eWeLink");
  lv_obj_t* tSw = lv_tabview_add_tab(s_tv, TR("Uređaji", "Devices"));
  lv_obj_t* tAlarm = lv_tabview_add_tab(s_tv, "Alarm");
  lv_obj_t* tWeb = lv_tabview_add_tab(s_tv, "Web");
  for (lv_obj_t* t : {tWifi, tTuya, tEw, tSw, tAlarm, tWeb}) {
    lv_obj_set_style_pad_all(t, 18, 0);
    lv_obj_set_style_pad_row(t, 8, 0);
  }
  buildWifiTab(tWifi);
  buildTuyaTab(tTuya);
  buildEWeLinkTab(tEw);
  buildSwitchesTab(tSw);
  buildAlarmTab(tAlarm);
  buildWebTab(tWeb);

  // Zajednička tastatura
  s_kb = lv_keyboard_create(s_scr);
  lv_obj_set_size(s_kb, LCD_H_RES, KB_H);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_20, 0);
  lv_obj_add_event_cb(s_kb, [](lv_event_t*) { hideKeyboard(); }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_kb, [](lv_event_t*) { hideKeyboard(); }, LV_EVENT_CANCEL, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  // Promena taba sakriva tastaturu
  lv_obj_add_event_cb(s_tv, [](lv_event_t*) { hideKeyboard(); }, LV_EVENT_VALUE_CHANGED, nullptr);
}

void open(int tab) {
  // Podešavanja (i QR za telefon) štiti isti PIN kao isključivanje alarma
  if (!isOpen() && settings::hasAlarmPin()) {
    pin_prompt::ask(TR("Podešavanja", "Settings"), [tab] { openUnlocked(tab); });
    return;
  }
  openUnlocked(tab);
}

void openUnlocked(int tab) {
  loadFields();
  s_seenSettings = settings::revision();
  s_lastIp = "";
  s_seenOauth = portal::oauthSeq();
  lv_obj_add_flag(s_ewQr, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_ewResult, "");
  lv_tabview_set_active(s_tv, tab, LV_ANIM_OFF);
  fillSwitchList();
  refreshPinForm();
  setStatus(s_pinStatus, settings::hasAlarmPin() ? TR("PIN je podešen", "PIN is set") : TR("PIN još nije podešen", "No PIN set yet"),
            C_MUTED);
  refresh();
  lv_screen_load(s_scr);
  if (tab == TAB_WIFI && cloud::scanSeq() == 0) {
    cloud::requestWifiScan();
    lv_label_set_text(s_scanLbl, TR("Skeniram…", "Scanning…"));
  }
}

void close() {
  hideKeyboard();
  lv_screen_load(s_main);
}

bool isOpen() { return s_scr && lv_screen_active() == s_scr; }

void update() {
  if (!isOpen()) return;
  if (cloud::scanSeq() != s_seenScan) {
    s_seenScan = cloud::scanSeq();
    fillScanList();
    lv_label_set_text(s_scanLbl, TR("Skeniraj", "Scan"));
  }
  if (portal::oauthSeq() != s_seenOauth) {
    s_seenOauth = portal::oauthSeq();
    setStatus(s_ewResult, portal::oauthMessage(), portal::oauthOk() ? C_OK : C_ERR);
    if (portal::oauthOk()) lv_obj_add_flag(s_ewQr, LV_OBJ_FLAG_HIDDEN);
  }
  // Podešavanja promenjena spolja (web stranica) dok je ekran otvoren: osveži polja, inače bi
  // "Sačuvaj" sa zastarelim (praznim) poljima obrisao upravo unete ključeve.
  // Ne diraj polja dok korisnik kuca (tastatura otvorena); osveži kad je zatvori.
  if (settings::revision() != s_seenSettings && lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
    s_seenSettings = settings::revision();
    loadFields();
  }
  if (g_store.layoutRevision() != s_seenLayout) fillSwitchList();
  static uint32_t lastRefresh = 0;
  if (g_store.revision() != s_seenStore || millis() - lastRefresh > 2000) {
    s_seenStore = g_store.revision();
    lastRefresh = millis();
    refresh();
  }
}

}  // namespace settings_ui
