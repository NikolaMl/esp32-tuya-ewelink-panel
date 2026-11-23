#pragma once
#include <ArduinoJson.h>

#include "net/cloud_provider.h"
#include "net/https_session.h"

// eWeLink Cloud API v2 (CoolKit open platform, lični developer nalog).
// Lični nalog podržava samo OAuth2 prijavu (preko browsera): panel prikaže QR kod,
// prijaviš se telefonom, panel dobije tokene, čuva ih u NVS-u i sam ih obnavlja.
// Dokumentacija: https://coolkit-technologies.github.io/eWeLink-API/#/en/OAuth2.0
class EWeLinkClient : public CloudProvider {
 public:
  EWeLinkClient(const char* appId, const char* appSecret);

  CloudKind kind() const override { return CloudKind::EWeLink; }
  bool configured() const override;
  bool fetch(std::vector<SwitchInfo>& out, String& err) override;
  bool setSwitch(const SwitchInfo& sw, bool on, String& err) override;

  // Učitaj tokene iz NVS-a. Pozvati pri startu i posle prijave/odjave.
  void loadTokens();
  bool hasTokens() const { return at_.length() > 0 && rt_.length() > 0; }
  bool sessionExpired() const { return sessionDead_; }

  static int channelsForUiid(int uiid);

  // ---- OAuth2 prijava (pokreće se sa panela ili web stranice)
  // Link ka eWeLink stranici za prijavu; posle prijave browser ide na redirectUrl?code=..&region=..&state=..
  static String oauthUrl(const String& appId, const String& appSecret, const String& redirectUrl,
                         const String& state);
  // Zameni "code" (važi 30 s) za tokene i sačuvaj ih u NVS
  static bool exchangeCode(const String& appId, const String& appSecret, const String& code,
                           const String& region, const String& redirectUrl, String& err);
  static void storeTokens(const String& at, const String& rt, const String& region, uint64_t atExpires);
  static void clearTokens();
  static bool tokensStored();

 private:
  bool ensureFresh(String& err);
  bool refresh(String& err);
  void saveTokens();
  bool call(const char* method, const String& pathAndQuery, const String& body, JsonDocument& doc,
            const JsonDocument* filter, String& err, bool allowRetry = true);
  String host() const;
  static String describeError(int code, const String& msg);

  String appId_, secret_;
  String at_, rt_, region_;
  uint64_t atExpires_ = 0;  // ms epoch (0 = nepoznato)
  bool sessionDead_ = false;
  HttpsSession http_{"ewelink"};
};
