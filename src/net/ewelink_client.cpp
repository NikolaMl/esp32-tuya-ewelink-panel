#include "net/ewelink_client.h"

#include <Preferences.h>

#include "util/crypto.h"
#include "util/i18n.h"

static constexpr uint64_t DAY_MS = 24ULL * 3600 * 1000;
static constexpr const char* NVS_NS = "ewelink";

EWeLinkClient::EWeLinkClient(const char* appId, const char* appSecret) : appId_(appId), secret_(appSecret) {}

bool EWeLinkClient::configured() const { return appId_.length() > 0 && secret_.length() > 0 && hasTokens(); }

static String hostFor(const String& region) {
  const String r = region.length() ? region : String("eu");
  return r == "cn" ? "https://cn-apia.coolkit.cn" : "https://" + r + "-apia.coolkit.cc";
}

String EWeLinkClient::host() const { return hostFor(region_); }

// ---------------------------------------------------------------- tokeni (NVS)

void EWeLinkClient::loadTokens() {
  Preferences p;
  p.begin(NVS_NS, true);
  at_ = p.isKey("at") ? p.getString("at", "") : "";
  rt_ = p.isKey("rt") ? p.getString("rt", "") : "";
  region_ = p.isKey("region") ? p.getString("region", "eu") : "eu";
  atExpires_ = p.isKey("atExp") ? p.getULong64("atExp", 0) : 0;
  p.end();
  sessionDead_ = false;
  log_i("[ewelink] tokeni iz NVS: %s, region=%s", hasTokens() ? "da" : "ne", region_.c_str());
}

void EWeLinkClient::storeTokens(const String& at, const String& rt, const String& region, uint64_t atExpires) {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("at", at);
  p.putString("rt", rt);
  p.putString("region", region);
  p.putULong64("atExp", atExpires);
  p.end();
}

void EWeLinkClient::saveTokens() { storeTokens(at_, rt_, region_, atExpires_); }

void EWeLinkClient::clearTokens() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
}

bool EWeLinkClient::tokensStored() {
  Preferences p;
  p.begin(NVS_NS, true);
  const bool has = p.isKey("rt") && p.getString("rt", "").length() > 0;
  p.end();
  return has;
}

// ---------------------------------------------------------------- OAuth2 prijava

String EWeLinkClient::oauthUrl(const String& appId, const String& appSecret, const String& redirectUrl,
                               const String& state) {
  char seq[24];
  snprintf(seq, sizeof(seq), "%llu", (unsigned long long)epochMillis());
  // authorization = Base64(HMAC-SHA256(key=appSecret, msg="<clientId>_<seq>"))
  const String sig = crypto::hmacSha256Base64(appSecret, appId + "_" + seq);
  return String("https://c2ccdn.coolkit.cc/oauth/index.html?clientId=") + crypto::urlEncode(appId) +
         "&seq=" + seq + "&authorization=" + crypto::urlEncode(sig) +
         "&redirectUrl=" + crypto::urlEncode(redirectUrl) + "&grantType=authorization_code" +
         "&state=" + crypto::urlEncode(state) + "&nonce=" + crypto::randomAlnum(8) + "&showQRCode=false";
}

bool EWeLinkClient::exchangeCode(const String& appId, const String& appSecret, const String& code,
                                 const String& region, const String& redirectUrl, String& err) {
  JsonDocument b;
  b["code"] = code;
  b["redirectUrl"] = redirectUrl;
  b["grantType"] = "authorization_code";
  String body;
  serializeJson(b, body);

  HeaderList h = {{"X-CK-Appid", appId},
                  {"X-CK-Nonce", crypto::randomAlnum(8)},
                  {"Authorization", "Sign " + crypto::hmacSha256Base64(appSecret, body)},
                  {"Content-Type", "application/json"}};
  HttpsSession http("ewelink-oauth");
  HttpResult r = http.request("POST", hostFor(region) + "/v2/user/oauth/token", body, h);
  http.reset();
  if (!r.ok()) {
    err = r.status < 0 ? String(TR("nema veze sa eWeLink serverom", "no connection to the eWeLink server")) : "HTTP " + String(r.status);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    err = TR("neispravan odgovor servera", "invalid server response");
    return false;
  }
  const int ecode = doc["error"] | -1;
  if (ecode != 0) {
    err = describeError(ecode, doc["msg"] | "");
    return false;
  }
  const String at = doc["data"]["accessToken"] | "";
  const String rt = doc["data"]["refreshToken"] | "";
  const uint64_t atExp = doc["data"]["atExpiredTime"] | 0ULL;
  if (!at.length() || !rt.length()) {
    err = TR("server nije vratio tokene", "server returned no tokens");
    return false;
  }
  storeTokens(at, rt, region.length() ? region : String("eu"), atExp);
  Serial.printf("[ewelink] prijava uspesna (region %s)\n", region.c_str());
  return true;
}

bool EWeLinkClient::refresh(String& err) {
  JsonDocument b;
  b["rt"] = rt_;
  String body;
  serializeJson(b, body);

  HeaderList h = {{"X-CK-Appid", appId_},
                  {"X-CK-Nonce", crypto::randomAlnum(8)},
                  {"Authorization", "Sign " + crypto::hmacSha256Base64(secret_, body)},
                  {"Content-Type", "application/json"}};
  HttpResult r = http_.request("POST", host() + "/v2/user/refresh", body, h);
  if (!r.ok()) {
    err = r.status < 0 ? String(TR("eWeLink: nema veze sa serverom", "eWeLink: no connection to the server")) : "eWeLink: HTTP " + String(r.status);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, r.body)) {
    err = TR("eWeLink: neispravan odgovor (refresh)", "eWeLink: invalid response (refresh)");
    return false;
  }
  const int code = doc["error"] | -1;
  if (code != 0) {
    if (code == 401 || code == 402 || code == 406) sessionDead_ = true;
    err = describeError(code, doc["msg"] | "");
    return false;
  }
  const String at = doc["data"]["at"] | "";
  const String rt = doc["data"]["rt"] | "";
  if (!at.length() || !rt.length()) {
    err = TR("eWeLink: refresh bez tokena", "eWeLink: refresh returned no token");
    return false;
  }
  at_ = at;
  rt_ = rt;
  atExpires_ = epochMillis() + 30 * DAY_MS;  // at vazi 30 dana, rt 60 dana
  saveTokens();
  log_i("[ewelink] token obnovljen");
  return true;
}

bool EWeLinkClient::ensureFresh(String& err) {
  if (sessionDead_) {
    err = TR("eWeLink: prijava istekla - ponovo poveži nalog (Podešavanja)",
             "eWeLink: login expired - link the account again (Settings)");
    return false;
  }
  const uint64_t now = epochMillis();
  // Obnovi nedelju dana pre isteka; ako rok ne znamo, obnovi odmah (i tako saznamo rok)
  if (atExpires_ == 0 || now + 7 * DAY_MS > atExpires_) return refresh(err);
  return true;
}

// ---------------------------------------------------------------- HTTP

String EWeLinkClient::describeError(int code, const String& msg) {
  String hint;
  switch (code) {
    case 401: hint = TR("sesija nevažeća - ponovo poveži nalog (Podešavanja)", "session invalid - link the account again (Settings)"); break;
    case 402: hint = TR("token istekao", "token expired"); break;
    case 403:
    case 412: hint = TR("mesečni limit API poziva je potrošen", "monthly API call limit reached"); break;
    case 406: hint = TR("odbijeno (potpis / dozvola)", "rejected (signature / permission)"); break;
    case 407: hint = TR("APPID nema dozvolu za ovaj API", "APPID has no permission for this API"); break;
    case 4002: hint = TR("uređaj nije prihvatio komandu (offline?)", "device did not accept the command (offline?)"); break;
    default: break;
  }
  return "eWeLink " + String(code) + ": " + (hint.length() ? hint : msg);
}

bool EWeLinkClient::call(const char* method, const String& pathAndQuery, const String& body, JsonDocument& doc,
                         const JsonDocument* filter, String& err, bool allowRetry) {
  if (!ensureFresh(err)) return false;

  HeaderList h = {{"Authorization", "Bearer " + at_},
                  {"X-CK-Appid", appId_},
                  {"X-CK-Nonce", crypto::randomAlnum(8)}};
  if (body.length()) h.push_back({"Content-Type", "application/json"});

  HttpResult r = http_.request(method, host() + pathAndQuery, body, h);
  if (r.status == 403 || r.status == 412) {
    err = describeError(412, "");
    return false;
  }
  if (!r.ok()) {
    err = r.status < 0 ? TR("eWeLink: nema veze sa serverom (", "eWeLink: no connection to the server (") + HTTPClient::errorToString(r.status) + ")"
                       : "eWeLink: HTTP " + String(r.status);
    return false;
  }
  doc.clear();
  DeserializationError de = filter ? deserializeJson(doc, r.body, DeserializationOption::Filter(*filter))
                                   : deserializeJson(doc, r.body);
  if (de) {
    err = TR("eWeLink: neispravan JSON (", "eWeLink: invalid JSON (") + String(de.c_str()) + ")";
    return false;
  }
  const int code = doc["error"] | -1;
  if (code == 0) return true;
  if (code == 402 && allowRetry) {
    if (!refresh(err)) return false;
    return call(method, pathAndQuery, body, doc, filter, err, false);
  }
  if (code == 401) sessionDead_ = true;
  err = describeError(code, doc["msg"] | "");
  return false;
}

// ---------------------------------------------------------------- uređaji

int EWeLinkClient::channelsForUiid(int uiid) {
  switch (uiid) {
    // dvokanalni
    case 2: case 7: case 29: case 82: case 113: case 126: case 139: case 161: case 165: case 289: case 2256:
      return 2;
    // trokanalni
    case 3: case 8: case 83: case 114: case 140: case 162: case 3256:
      return 3;
    // četvorokanalni
    case 4: case 9: case 31: case 84: case 130: case 141: case 163: case 290: case 4256:
      return 4;
    // jednokanalni koji koriste "switches" niz (UIID 77, 78, 107, 112, 138, 160, 182, ...)
    case 77: case 78: case 81: case 107: case 112: case 138: case 160: case 182:
      return 1;
    default:
      return 0;  // nepoznato
  }
}

bool EWeLinkClient::fetch(std::vector<SwitchInfo>& out, String& err) {
  JsonDocument filter;
  filter["error"] = true;
  filter["msg"] = true;
  filter["data"]["total"] = true;
  JsonObject ft = filter["data"]["thingList"].add<JsonObject>();
  ft["itemType"] = true;
  JsonObject fi = ft["itemData"].to<JsonObject>();
  fi["name"] = true;
  fi["deviceid"] = true;
  fi["online"] = true;
  fi["params"]["switch"] = true;
  fi["params"]["switches"] = true;
  fi["extra"]["uiid"] = true;
  fi["tags"]["ck_channel_name"] = true;

  JsonDocument doc;
  if (!call("GET", "/v2/device/thing?num=0", "", doc, &filter, err)) return false;

  out.clear();
  for (JsonObject thing : doc["data"]["thingList"].as<JsonArray>()) {
    const int itemType = thing["itemType"] | 0;
    if (itemType != 1 && itemType != 2) continue;  // 3 = grupa
    JsonObject d = thing["itemData"];
    const String id = d["deviceid"] | "";
    const String devName = d["name"] | id;
    const bool online = d["online"] | false;
    JsonObject params = d["params"];
    const DeviceGroup group = groupForEwelinkUiid(d["extra"]["uiid"] | 0);

    if (params["switch"].is<const char*>()) {
      SwitchInfo s;
      s.cloud = CloudKind::EWeLink;
      s.deviceId = id;
      s.channel = "";
      s.isMultiChannel = false;
      s.name = devName;
      s.on = String(params["switch"].as<const char*>()) == "on";
      s.online = online;
      s.group = group;
      out.push_back(s);
      continue;
    }

    JsonArray sw = params["switches"];
    if (sw.isNull() || sw.size() == 0) continue;

    // Broj kanala: imena kanala iz aplikacije > tabela UIID > dužina niza (max 4)
    JsonObject chNames = d["tags"]["ck_channel_name"];
    int count = channelsForUiid(d["extra"]["uiid"] | 0);
    if (count == 0 && !chNames.isNull() && chNames.size() > 0) count = chNames.size();
    if (count == 0) count = sw.size() > 4 ? 4 : sw.size();

    for (JsonObject ch : sw) {
      const int outlet = ch["outlet"] | 0;
      if (outlet >= count) continue;
      SwitchInfo s;
      s.cloud = CloudKind::EWeLink;
      s.deviceId = id;
      s.channel = String(outlet);
      s.isMultiChannel = count > 1;
      const String chName = chNames[String(outlet)] | "";
      if (count == 1) s.name = devName;
      else if (chName.length()) s.name = devName + " - " + chName;
      else s.name = devName + " " + String(outlet + 1);
      s.on = String(ch["switch"] | "off") == "on";
      s.online = online;
      s.group = group;
      out.push_back(s);
    }
  }
  return true;
}

bool EWeLinkClient::setSwitch(const SwitchInfo& sw, bool on, String& err) {
  JsonDocument b;
  b["type"] = 1;
  b["id"] = sw.deviceId;
  if (sw.channel.length() == 0) {
    b["params"]["switch"] = on ? "on" : "off";
  } else {
    JsonObject ch = b["params"]["switches"].add<JsonObject>();
    ch["switch"] = on ? "on" : "off";
    ch["outlet"] = sw.channel.toInt();
  }
  String body;
  serializeJson(b, body);
  JsonDocument doc;
  return call("POST", "/v2/device/thing/status", body, doc, nullptr, err);
}
