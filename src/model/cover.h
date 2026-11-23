#pragma once
#include <Arduino.h>

// Roletna / modul za roletne (Tuya kategorije "cl" i "clkg"): DP "control" = open / stop / close,
// "percent_state" ili "percent_control" = položaj u procentima (ako modul to javlja).
enum class CoverAction : uint8_t { Open, Stop, Close };

inline const char* coverActionToTuya(CoverAction a) {
  switch (a) {
    case CoverAction::Open: return "open";
    case CoverAction::Close: return "close";
    default: return "stop";
  }
}

struct CoverInfo {
  String deviceId;
  String controlCode = "control";  // "control", kod dvostrukih modula "control_2"
  String name;
  bool online = false;
  int8_t position = -1;  // 0..100 kako ga javlja modul, -1 = ne javlja (nije kalibrisan)
  String lastAction;     // poslednja vrednost DP "control" (open / stop / close / continue)
  bool isMultiChannel = false;

  // Ključ za raspored i sakrivanje (isti prostor ključeva kao prekidači)
  String key() const { return isMultiChannel ? deviceId + "/" + controlCode : deviceId; }
};

struct CoverState : CoverInfo {
  bool pending = false;    // komanda poslata, čeka se odgovor
  uint32_t holdUntil = 0;  // do tada ignorišemo staro "control" iz osvežavanja
};
