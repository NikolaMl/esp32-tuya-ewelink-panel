#include "util/restart.h"

#include <Arduino.h>
#include <esp_attr.h>

#include <atomic>

#include "board/display.h"

namespace restart {

static std::atomic<uint32_t> s_at{0};  // millis() kada se restartuje (0 = nije zatraženo)

// Preživljava softverski restart i watchdog (ne i isključenje napajanja)
RTC_NOINIT_ATTR static uint32_t s_rtcFlag;
static constexpr uint32_t RTC_FLAG = 0x52535431;  // "RST1"

bool wasRequested() {
  static int8_t s_was = -1;
  if (s_was < 0) {
    s_was = s_rtcFlag == RTC_FLAG ? 1 : 0;
    s_rtcFlag = 0;
  }
  return s_was == 1;
}

void soon(uint32_t delayMs) {
  uint32_t at = millis() + delayMs;
  if (!at) at = 1;
  s_at.store(at);
  Serial.printf("[main] restart za %u ms\n", (unsigned)delayMs);
}

bool pending() { return s_at.load() != 0; }

void poll() {
  const uint32_t at = s_at.load();
  if (at && (int32_t)(millis() - at) >= 0) {
    Serial.println("[main] restart");
    Serial.flush();
    wasRequested();  // pročitaj staru oznaku pre nego što je upišemo
    s_rtcFlag = RTC_FLAG;
    display::shutdown();
    ESP.restart();
  }
}

}  // namespace restart
