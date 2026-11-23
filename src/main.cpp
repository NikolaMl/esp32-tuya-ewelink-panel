// ESP32 Tuya & eWeLink Panel — ESP32-S3 5" (Sunton ESP32-8048S050C)
// Uključivanje/isključivanje svetla iz Smart Life (Tuya) i eWeLink (Sonoff) naloga.

#include <Arduino.h>
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <lvgl.h>

#include "board/display.h"
#include "board/touch_gt911.h"
#include "config.h"
#include "debug/console.h"
#include "model/device_store.h"
#include "model/settings.h"
#include "net/cloud_task.h"
#include "net/web_portal.h"
#include "model/scenes.h"
#include "ui/alarm_screen.h"
#include "ui/background.h"
#include "ui/smoke_alert.h"
#include "ui/settings_screen.h"
#include "ui/ui.h"
#include "util/restart.h"
#include "util/selftest.h"

// LVGL + TinyTTF rasterizacija traže više steka od podrazumevanih 8 KB
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---------------------------------------------------------------- ekran: budno / zatamnjeno / ugašeno
enum class ScreenState { Awake, Dim, Off };
static ScreenState s_screen = ScreenState::Awake;
static uint32_t s_lastTouch = 0;

// Konzola (debug): komanda se računa kao dodir — ekran se upali i ne gasi (gašenje zatvara prozorčiće)
void consoleWake() {
  s_lastTouch = millis();
  if (s_screen != ScreenState::Awake) {
    s_screen = ScreenState::Awake;
    display::setBacklight(BACKLIGHT_FULL);
  }
}
static bool s_swallow = false;  // prvi dodir samo budi ekran, ne pali svetlo

static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
  int16_t x = 0, y = 0;
  bool pressed = touch::read(x, y);
  if (debug::injectedTouch(x, y)) pressed = true;
  if (pressed) {
    s_lastTouch = millis();
    cloud::notifyActivity();
    if (s_screen != ScreenState::Awake) {
      s_screen = ScreenState::Awake;
      display::setBacklight(BACKLIGHT_FULL);
      s_swallow = true;
      Serial.println("[ekran] probuđen dodirom");
    }
  } else {
    s_swallow = false;
  }
  data->point.x = x;
  data->point.y = y;
  data->state = (pressed && !s_swallow) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void updateScreenPower() {
  // Alarm, izlazno kašnjenje ili dim: ekran pun sjaj, bez gašenja (dodir tada odmah radi)
  if (alarm_ui::wantsAwake() || smoke_alert::wantsAwake()) {
    s_lastTouch = millis();
    if (s_screen != ScreenState::Awake) {
      s_screen = ScreenState::Awake;
      display::setBacklight(BACKLIGHT_FULL);
    }
    return;
  }
  const uint32_t idle = millis() - s_lastTouch;
  const bool settingsOpen = settings_ui::isOpen();
  const settings::ScreenCfg sc = settings::screen();  // menja se sa telefona
  const uint32_t dimAfter = settingsOpen ? SETTINGS_DIM_AFTER_MS : sc.dimSec * 1000UL;
  const uint32_t offAfter = settingsOpen ? SETTINGS_OFF_AFTER_MS : sc.offSec * 1000UL;
  if (s_screen == ScreenState::Awake && idle > dimAfter) {
    s_screen = ScreenState::Dim;
    display::setBacklight(BACKLIGHT_DIM);
    Serial.println("[ekran] zatamnjen");
  } else if (s_screen == ScreenState::Dim && offAfter > 0 && idle > offAfter) {
    s_screen = ScreenState::Off;
    display::setBacklight(0);
    ui::goHome();  // posle buđenja uvek početni ekran
    Serial.println("[ekran] ugašen");
  }
}

// ---------------------------------------------------------------- dijagnostika

static const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "uključenje";
    case ESP_RST_SW: return "softverski restart";
    case ESP_RST_PANIC: return "PAD (izuzetak)";
    case ESP_RST_INT_WDT: return "PAD (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "PAD (task watchdog)";
    case ESP_RST_WDT: return "PAD (watchdog)";
    case ESP_RST_BROWNOUT: return "pad napona (brownout)";
    case ESP_RST_DEEPSLEEP: return "buđenje iz sna";
    case ESP_RST_EXT: return "spoljni reset (USB/taster)";
    default: return "nepoznat";
  }
}

// Razlog restarta i sažetak poslednjeg pada iz coredump particije (dekodira se sa firmware.elf:
// xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/panel/firmware.elf <adrese>)
static void logBootDiagnostics() {
  const esp_reset_reason_t r = esp_reset_reason();
  Serial.printf("[main] razlog pokretanja: %s (%d)%s\n", resetReasonText(r), (int)r,
                restart::wasRequested() ? ", restart je zatražio panel" : "");
  if (esp_core_dump_image_check() != ESP_OK) return;
  esp_core_dump_summary_t* s = static_cast<esp_core_dump_summary_t*>(malloc(sizeof(esp_core_dump_summary_t)));
  if (!s) return;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char sha[9] = {0};
    esp_app_get_elf_sha256(sha, sizeof(sha));
    const bool same = strncmp(sha, (const char*)s->app_elf_sha256, 8) == 0;
    Serial.printf("[main] poslednji pad (%s): task '%s', PC 0x%08lx, backtrace:", same ? "ova verzija" : "starija verzija",
                  s->exc_task, (unsigned long)s->exc_pc);
    for (uint32_t i = 0; i < s->exc_bt_info.depth; i++) Serial.printf(" 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
    Serial.println(s->exc_bt_info.corrupted ? " (nepotpun)" : "");
  }
  free(s);
}

// ---------------------------------------------------------------- start

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Smart Light Panel ===");
  Serial.printf("PSRAM: %u KB slobodno, heap: %u KB slobodno\n", (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(ESP.getFreeHeap() / 1024));
  logBootDiagnostics();

  g_store.begin();
  settings::begin();
  if (background::fsReady()) scenes::begin();
  selftest::run();

  if (!display::begin()) {
    Serial.println("[main] ekran nije pokrenut - zaustavljeno");
    for (;;) delay(1000);
  }
  touch::begin();

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchRead);

  ui::begin(display::lv());
  lv_timer_handler();  // prvi kadar pre paljenja pozadinskog svetla
  display::setBacklight(BACKLIGHT_FULL);
  s_lastTouch = millis();

  cloud::begin();
  portal::begin();
  Serial.printf("[main] spremno. heap: %u KB, PSRAM: %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)(ESP.getFreePsram() / 1024));
}

void loop() {
  const uint32_t wait = lv_timer_handler();
  debug::poll();
  ui::update();
  updateScreenPower();
  restart::poll();
  delay(wait < 5 ? wait : 5);
}
