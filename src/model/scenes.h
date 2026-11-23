#pragma once
#include <Arduino.h>

#include <vector>

// Scene: jedno dugme podesi više uređaja (npr. "Odlazim": ugasi svetla, spusti roletne, uključi alarm).
// Prave se na telefonu (tab Scene), čuvaju u LittleFS (/scenes.json), pokreću sa panela.
namespace scenes {

// op: "on" / "off" (prekidač), "open" / "close" / "stop" (roletna), "away" / "home" / "disarm" (alarm)
struct Action {
  String key;
  String op;
};

struct Scene {
  String id;  // kratak, nepromenljiv (oznaka na slici je "scene:<id>")
  String name;
  std::vector<Action> actions;
};

constexpr size_t MAX_SCENES = 16, MAX_ACTIONS = 32;

void begin();  // posle background::fsReady()
std::vector<Scene> all();
void setDemo(const std::vector<Scene>& list);  // demo prikaz: samo u RAM-u, do restarta
bool find(const String& id, Scene& out);
bool save(const std::vector<Scene>& list);  // web task: zameni sve i snimi
uint32_t revision();

inline String keyFor(const String& id) { return "scene:" + id; }
inline bool isSceneKey(const String& key) { return key.startsWith("scene:"); }
inline String idOf(const String& key) { return key.substring(6); }

// Pokretanje sa telefona: web task samo ostavi zahtev, UI task ga izvrši (isti put kao dodir na panelu)
void requestRun(const String& id);
String takeRunRequest();

}  // namespace scenes
