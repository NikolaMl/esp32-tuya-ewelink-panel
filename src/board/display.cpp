#include "board/display.h"

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/semphr.h>
#include <string.h>

#include "config.h"

namespace display {

static esp_lcd_panel_handle_t s_panel = nullptr;
static SemaphoreHandle_t s_vsync = nullptr;
static lv_display_t* s_disp = nullptr;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_vsync, &woken);
  return woken == pdTRUE;
}

// Dijagnostika (konzola "rf"): svako iscrtavanje u log — koliko piksela, koje oblasti, koliko ms.
// Veliko iscrtavanje preko fotografije opterećuje PSRAM i može da izazove treperenje.
static volatile bool s_refrLog = false;
static uint32_t s_refrT0 = 0, s_refrPx = 0, s_refrAreas = 0;
static char s_refrList[96];

static void onRenderStart(lv_event_t*) {
  s_refrT0 = millis();
  s_refrPx = 0;
  s_refrAreas = 0;
  s_refrList[0] = 0;
}

// LVGL crta direktno u jedan od dva framebuffer-a panela (DIRECT mode).
// Kad je kadar gotov, prebacujemo panel na taj buffer i čekamo VSYNC — bez cepanja slike.
static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  if (s_refrLog) {
    s_refrPx += lv_area_get_size(area);
    if (s_refrAreas++ < 4) {
      const size_t n = strlen(s_refrList);
      snprintf(s_refrList + n, sizeof(s_refrList) - n, " %d,%d-%d,%d", (int)area->x1, (int)area->y1, (int)area->x2,
               (int)area->y2);
    }
  }
  if (lv_display_flush_is_last(disp) && s_panel) {
    if (s_refrLog && s_refrPx)
      Serial.printf("[refr] %lu ms: %lu px (%lu%%), %lu obl.%s\n", (unsigned long)(millis() - s_refrT0),
                    (unsigned long)s_refrPx, (unsigned long)(s_refrPx * 100 / (LCD_H_RES * LCD_V_RES)),
                    (unsigned long)s_refrAreas, s_refrList);
    xSemaphoreTake(s_vsync, 0);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, px);
    xSemaphoreTake(s_vsync, pdMS_TO_TICKS(100));
  }
  lv_display_flush_ready(disp);
}

void setRefreshLog(bool on) { s_refrLog = on; }
bool refreshLog() { return s_refrLog; }

static uint32_t tickCb() { return millis(); }

static void logCb(lv_log_level_t, const char* buf) { Serial.print(buf); }

void setBacklight(uint8_t level) { ledcWrite(PIN_LCD_BL, level); }

lv_display_t* lv() { return s_disp; }

bool setPclk(uint32_t hz) { return s_panel && esp_lcd_rgb_panel_set_pclk(s_panel, hz) == ESP_OK; }

bool begin() {
  ledcAttach(PIN_LCD_BL, 5000, 8);
  setBacklight(0);  // ugašeno dok se ne iscrta prvi kadar

  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = LCD_H_RES;
  cfg.timings.v_res = LCD_V_RES;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch = LCD_HSYNC_BACK;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch = LCD_VSYNC_BACK;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT;
  cfg.timings.flags.pclk_active_neg = LCD_PCLK_ACTIVE_NEG;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  // Bounce buffer u internom RAM-u: LCD DMA ne čita direktno iz PSRAM-a,
  // pa se slika ne "pomera" kad Wi-Fi/TLS opterete memoriju.
  cfg.bounce_buffer_size_px = LCD_H_RES * 10;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = PIN_LCD_HSYNC;
  cfg.vsync_gpio_num = PIN_LCD_VSYNC;
  cfg.de_gpio_num = PIN_LCD_DE;
  cfg.pclk_gpio_num = PIN_LCD_PCLK;
  cfg.disp_gpio_num = -1;
  const int data[16] = PIN_LCD_DATA;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err != ESP_OK) {
    Serial.printf("[display] esp_lcd_new_rgb_panel: %s\n", esp_err_to_name(err));
    return false;
  }

  s_vsync = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs;
  memset(&cbs, 0, sizeof(cbs));
  cbs.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr);

  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

  void* fb0 = nullptr;
  void* fb1 = nullptr;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1));

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_log_register_print_cb(logCb);

  s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s_disp, fb0, fb1, LCD_H_RES * LCD_V_RES * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(s_disp, flushCb);
  lv_display_add_event_cb(s_disp, onRenderStart, LV_EVENT_RENDER_START, nullptr);

  Serial.printf("[display] %dx%d RGB panel OK, fb0=%p fb1=%p\n", LCD_H_RES, LCD_V_RES, fb0, fb1);
  return true;
}

void shutdown() {
  setBacklight(0);
  // RGB panel stalno čita framebuffer iz PSRAM-a (bounce buffer prekid). Ako to radi dok
  // esp_restart() gasi keš, restart se zaglavi i okine watchdog — zato ga prvo zaustavi.
  if (s_panel) {
    esp_lcd_panel_del(s_panel);
    s_panel = nullptr;
  }
}

}  // namespace display
