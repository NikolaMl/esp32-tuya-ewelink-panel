#include "board/touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace touch {

static constexpr uint16_t REG_CONFIG_XMAX = 0x8048;
static constexpr uint16_t REG_PRODUCT_ID = 0x8140;
static constexpr uint16_t REG_STATUS = 0x814E;
static constexpr uint16_t REG_POINT1 = 0x814F;

static uint8_t s_addr = 0;
static uint16_t s_xMax = LCD_H_RES, s_yMax = LCD_V_RES;
static bool s_pressed = false;
static int16_t s_x = 0, s_y = 0;

static bool readReg(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(s_addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool writeReg8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool probe() {
  for (uint8_t a : {0x5D, 0x14}) {
    s_addr = a;
    uint8_t id[4] = {0};
    if (readReg(REG_PRODUCT_ID, id, 4) && id[0] == '9') {
      Serial.printf("[touch] GT%c%c%c na adresi 0x%02X\n", id[0], id[1], id[2], a);
      return true;
    }
  }
  s_addr = 0;
  return false;
}

bool begin() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
  if (!probe() && PIN_TOUCH_RST >= 0) {
    // Reset kontrolera i ponovni pokušaj
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(100);
    probe();
  }
  if (!s_addr) {
    Serial.println("[touch] GT911 nije pronadjen!");
    return false;
  }
  // Rezolucija iz GT911 konfiguracije (neke ploče imaju drugačiju od ekrana -> skaliramo)
  uint8_t r[4];
  if (readReg(REG_CONFIG_XMAX, r, 4)) {
    const uint16_t xm = r[0] | (r[1] << 8), ym = r[2] | (r[3] << 8);
    if (xm > 0 && ym > 0 && xm < 4096 && ym < 4096) {
      s_xMax = xm;
      s_yMax = ym;
    }
  }
  Serial.printf("[touch] rezolucija kontrolera %ux%u\n", s_xMax, s_yMax);
  return true;
}

bool read(int16_t& x, int16_t& y) {
  if (!s_addr) return false;
  uint8_t status = 0;
  if (readReg(REG_STATUS, &status, 1) && (status & 0x80)) {
    const uint8_t n = status & 0x0F;
    if (n > 0 && n <= 5) {
      uint8_t p[8];
      if (readReg(REG_POINT1, p, sizeof(p))) {
        int32_t rx = p[1] | (p[2] << 8);
        int32_t ry = p[3] | (p[4] << 8);
        if (s_xMax != LCD_H_RES) rx = rx * LCD_H_RES / s_xMax;
        if (s_yMax != LCD_V_RES) ry = ry * LCD_V_RES / s_yMax;
        s_x = constrain(rx, 0, LCD_H_RES - 1);
        s_y = constrain(ry, 0, LCD_V_RES - 1);
        s_pressed = true;
      }
    } else {
      s_pressed = false;
    }
    writeReg8(REG_STATUS, 0);
  }
  x = s_x;
  y = s_y;
  return s_pressed;
}

}  // namespace touch
