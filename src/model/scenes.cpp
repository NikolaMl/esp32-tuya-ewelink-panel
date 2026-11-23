#include "model/scenes.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

namespace scenes {

static const char* const PATH = "/scenes.json";
static const char* const TMP = "/scenes.tmp";

static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static std::vector<Scene> s_list;
static std::atomic<uint32_t> s_rev{1};
static String s_run;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

static void fromJson(JsonArrayConst arr, std::vector<Scene>& out) {
  out.clear();
  for (JsonObjectConst o : arr) {
    if (out.size() >= MAX_SCENES) break;
    Scene sc;
    sc.id = o["id"] | "";
    sc.name = o["n"] | "";
    if (sc.id.isEmpty()) continue;
    for (JsonObjectConst a : o["a"].as<JsonArrayConst>()) {
      if (sc.actions.size() >= MAX_ACTIONS) break;
      sc.actions.push_back({a["k"] | "", a["o"] | ""});
    }
    out.push_back(sc);
  }
}

void begin() {
  File f = LittleFS.open(PATH, "r");
  if (!f) {
    Serial.println("[scene] nema sačuvanih scena");
    return;
  }
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    Serial.printf("[scene] %s neispravan: %s\n", PATH, e.c_str());
    return;
  }
  Lock l;
  fromJson(doc.as<JsonArrayConst>(), s_list);
  Serial.printf("[scene] učitano: %u\n", (unsigned)s_list.size());
}

// Demo prikaz (konzola "show"): zamenske scene samo u RAM-u, sačuvane ostaju netaknute
static bool s_demoOn = false;
static std::vector<Scene> s_demo;

void setDemo(const std::vector<Scene>& list) {
  Lock l;
  s_demo = list;
  s_demoOn = true;
}

std::vector<Scene> all() {
  Lock l;
  return s_demoOn ? s_demo : s_list;
}

bool find(const String& id, Scene& out) {
  Lock l;
  for (const Scene& s : s_demoOn ? s_demo : s_list) {
    if (s.id == id) {
      out = s;
      return true;
    }
  }
  return false;
}

bool save(const std::vector<Scene>& list) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Scene& s : list) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = s.id;
    o["n"] = s.name;
    JsonArray a = o["a"].to<JsonArray>();
    for (const Action& x : s.actions) {
      JsonObject j = a.add<JsonObject>();
      j["k"] = x.key;
      j["o"] = x.op;
    }
  }
  // Prvo privremeni fajl, pa zamena: nedovršen upis ne briše stare scene
  File f = LittleFS.open(TMP, "w");
  if (!f) return false;
  const bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok || (LittleFS.exists(PATH) && !LittleFS.remove(PATH)) || !LittleFS.rename(TMP, PATH)) {
    LittleFS.remove(TMP);
    return false;
  }
  {
    Lock l;
    s_list = list;
  }
  s_rev++;
  return true;
}

uint32_t revision() { return s_rev; }

void requestRun(const String& id) {
  Lock l;
  s_run = id;
}

String takeRunRequest() {
  Lock l;
  String id = s_run;
  s_run = "";
  return id;
}

}  // namespace scenes
