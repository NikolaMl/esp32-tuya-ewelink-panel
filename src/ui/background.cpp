#include "ui/background.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <fcntl.h>
#include <freertos/semphr.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>

#include "config.h"

extern const uint8_t bg_start[] asm("_binary_assets_background_rgb565_start");
extern const uint8_t bg_end[] asm("_binary_assets_background_rgb565_end");

namespace background {

// Dva bafera: prikazani i onaj u koji se učitava sledeći deo (LVGL crta direktno iz bafera)
static lv_image_dsc_t s_dsc[2];
static uint8_t* s_buf[2] = {nullptr, nullptr};
static int s_cur = -1;  // bafer koji je trenutno na ekranu
static bool s_fs = false;
// Koji deo je u kom baferu (-1 = nijedan / upravo se puni) i brojač započetih učitavanja: telefon
// dobija sliku iz PSRAM-a umesto iz flash-a, a ako panel u međuvremenu počne da puni bafer, ostatak
// ide iz fajla.
static std::atomic<int> s_bufPage[2] = {{-1}, {-1}};
static std::atomic<uint32_t> s_loads{0};
// Da li deo ima fajl (-1 = nije provereno). Čuva se da web stranica (na 15 s) ne bi stalno čitala flash.
static int8_t s_custom[HOME_MAX_PAGES];
// UI task čita fajl, web task ga briše/zamenjuje: nikad u isto vreme (otvoren fajl se ne može obrisati)
static SemaphoreHandle_t s_fsMutex = xSemaphoreCreateMutex();
struct FsLock {
  FsLock() { xSemaphoreTake(s_fsMutex, portMAX_DELAY); }
  ~FsLock() { xSemaphoreGive(s_fsMutex); }
};

static bool embeddedOk() { return (size_t)(bg_end - bg_start) >= SIZE; }

static String path(uint8_t page) { return page == 0 ? String("/bg.rgb565") : "/bg" + String(page) + ".rgb565"; }
static String vfsPath(uint8_t page) { return "/littlefs" + path(page); }

// Čitanje flash-a u malim komadima, direktno (bez Arduino File, koji čita kroz bafer od 4 KB).
// Dok traje jedno čitanje flash-a, keš je isključen i procesor ne može da dopuni bafer iz kog ekran
// dobija sliku (rezerva ~0,3 ms). Čitanje od 4 KB traje duže -> crtice i pomeranje slike na ekranu
// dok se fotografija šalje telefonu ili učitava pri promeni dela. 512 B traje ~0,05 ms.
static constexpr size_t READ_PIECE = 512;
static bool readGently(int fd, uint8_t* dst, size_t n) {
  for (size_t done = 0; done < n;) {
    const ssize_t k = ::read(fd, dst + done, std::min(READ_PIECE, n - done));
    if (k <= 0) return false;
    done += k;
  }
  return true;
}

static void forgetFiles() {  // fajlovi promenjeni (pod FsLock-om)
  memset(s_custom, -1, sizeof(s_custom));
  s_bufPage[0] = -1;
  s_bufPage[1] = -1;
  s_loads++;
}

bool fsReady() {
  static bool once = false;
  if (!once) {
    once = true;
    // Particija "spiffs" iz partitions.csv; prvi put se formatira
    memset(s_custom, -1, sizeof(s_custom));
    s_fs = LittleFS.begin(true, "/littlefs", 4, "spiffs");
    if (s_fs)
      Serial.printf("[ui] LittleFS: %u KB slobodno od %u KB\n", (unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024),
                    (unsigned)(LittleFS.totalBytes() / 1024));
    else
      Serial.println("[ui] LittleFS: nije dostupan");
  }
  return s_fs;
}

static std::atomic<bool> s_builtinOnly{false};

void setBuiltinOnly(bool on) {
  FsLock lock;
  s_builtinOnly = on;
  forgetFiles();  // PSRAM kopija može biti fotografija sa telefona
}

static bool customLocked(uint8_t page) {
  if (page >= HOME_MAX_PAGES || !fsReady()) return false;
  if (s_builtinOnly && page == 0) return false;
  if (s_custom[page] < 0) {
    struct stat st;
    s_custom[page] = ::stat(vfsPath(page).c_str(), &st) == 0 && st.st_size == (off_t)SIZE;
  }
  return s_custom[page] == 1;
}

static bool loadInto(uint8_t* buf, uint8_t page) {
  FsLock lock;
  if (customLocked(page)) {
    const int fd = ::open(vfsPath(page).c_str(), O_RDONLY);
    const bool ok = fd >= 0 && readGently(fd, buf, SIZE);
    if (fd >= 0) ::close(fd);
    if (ok) return true;
    Serial.printf("[ui] pozadina %u: fajl neispravan\n", page);
  }
  if (page != 0 || !embeddedOk()) return false;
  memcpy(buf, bg_start, SIZE);
  return true;
}

const lv_image_dsc_t* load(uint8_t page) {
  const int idx = s_cur < 0 ? 0 : s_cur ^ 1;
  if (!s_buf[idx]) {
    s_buf[idx] = static_cast<uint8_t*>(heap_caps_malloc(SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_buf[idx]) {
      Serial.println("[ui] pozadina: nema memorije");
      return nullptr;
    }
  }
  const uint32_t t0 = millis();
  s_bufPage[idx] = -1;  // telefon više ne čita iz ovog bafera
  s_loads++;
  if (!loadInto(s_buf[idx], page)) return nullptr;
  lv_image_dsc_t& d = s_dsc[idx];
  memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = LCD_H_RES;
  d.header.h = LCD_V_RES;
  d.header.stride = LCD_H_RES * 2;
  d.data_size = SIZE;
  d.data = s_buf[idx];
  lv_image_cache_drop(&d);
  s_cur = idx;
  s_bufPage[idx] = page;
  log_i("[ui] pozadina dela %u ucitana za %u ms", page, (unsigned)(millis() - t0));
  return &d;
}

bool hasCustom(uint8_t page) {
  FsLock lock;
  return customLocked(page);
}

bool hasImage(uint8_t page) { return hasCustom(page) || (page == 0 && embeddedOk()); }

bool stream(uint8_t page, const std::function<void(const uint8_t*, size_t)>& sink) {
  static constexpr size_t CHUNK = 8192;
  bool custom;
  int fd = -1;
  {
    FsLock lock;
    custom = customLocked(page);
    // Fajl ostaje otvoren dok se šalje; briše ga samo web task (ovaj isti), pa nema sukoba
    if (custom) fd = ::open(vfsPath(page).c_str(), O_RDONLY);
  }
  if (!custom && (page != 0 || !embeddedOk())) return false;
  if (custom && fd < 0) return false;
  uint8_t* buf = static_cast<uint8_t*>(malloc(CHUNK));
  if (!buf) {
    if (fd >= 0) ::close(fd);
    return false;
  }
  size_t off = 0;
  // 1) Deo je već u PSRAM-u (prikazan ili prethodno prikazan): bez čitanja flash-a
  const uint32_t loads = s_loads;  // pre provere bafera (vidi load())
  const int idx = s_bufPage[0] == page ? 0 : s_bufPage[1] == page ? 1 : -1;
  if (idx >= 0) {
    while (off < SIZE) {
      const size_t n = std::min(CHUNK, SIZE - off);
      memcpy(buf, s_buf[idx] + off, n);
      if (s_loads != loads) break;  // panel puni bafer (promena dela): ostatak iz fajla
      sink(buf, n);
      off += n;
    }
  }
  // 2) Ostatak: ugrađena slika (mapirana, bez čitanja flash-a) ili fajl u malim komadima
  bool ok = true;
  while (ok && off < SIZE) {
    const size_t n = std::min(CHUNK, SIZE - off);
    if (!custom) {
      sink(bg_start + off, n);
    } else {
      ok = ::lseek(fd, off, SEEK_SET) == (off_t)off && readGently(fd, buf, n);
      if (ok) sink(buf, n);
    }
    off += n;
  }
  free(buf);
  if (fd >= 0) ::close(fd);
  return ok;
}

static std::atomic<bool> s_changed{false};
void markChanged() { s_changed = true; }
bool consumeChanged() { return s_changed.exchange(false); }

bool removeCustom(uint8_t page) {
  FsLock lock;
  forgetFiles();
  return fsReady() && (!LittleFS.exists(path(page)) || LittleFS.remove(path(page)));
}

bool commitUpload(uint8_t page) {
  FsLock lock;
  forgetFiles();
  if (!fsReady()) return false;
  if (LittleFS.exists(path(page))) LittleFS.remove(path(page));
  return LittleFS.rename(TMP_PATH, path(page));
}

bool deletePage(uint8_t page, uint8_t count) {
  FsLock lock;
  forgetFiles();
  if (!fsReady()) return false;
  if (LittleFS.exists(path(page))) LittleFS.remove(path(page));
  bool ok = true;
  for (uint8_t i = page + 1; i < count; i++) {
    if (LittleFS.exists(path(i))) ok = LittleFS.rename(path(i), path(i - 1)) && ok;
  }
  // Deo 0 obrisan: novi prvi deo (bivši drugi) bez svoje slike bi dobio ugrađenu; to je u redu
  return ok;
}

}  // namespace background
