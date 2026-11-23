#include "util/selftest.h"

#include <Arduino.h>

#include "net/tuya_client.h"
#include "util/crypto.h"

namespace selftest {

static bool check(const char* name, const String& got, const char* expected) {
  const bool ok = got == expected;
  Serial.printf("[selftest] %-16s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok) Serial.printf("[selftest]   dobijeno:  %s\n[selftest]   ocekivano: %s\n", got.c_str(), expected);
  return ok;
}

static String tuyaSign(const char* token, const char* method, const String& body, const String& pq) {
  return TuyaClient::buildSign("testid123", "testsecret456", token, "1700000000000",
                               TuyaClient::buildStringToSign(method, body, pq));
}

bool run() {
  bool ok = true;
  ok &= check("sha256", crypto::sha256Hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  // Zvanični primer iz eWeLink OAuth2.0 dokumentacije
  ok &= check("ewelink-hmac", crypto::hmacSha256Base64("abc", "ABC_123"),
              "v1+mfNY2ukxswM8sZOTg99srZsVnUVv9DGXeav1096M=");
  ok &= check("tuya-token", tuyaSign("", "GET", "", "/v1.0/token?grant_type=1"),
              "4584F56FBA081020FE49246164AFC15958E1B75DD3803346E6E5DF5E2340A67B");
  ok &= check("tuya-command",
              tuyaSign("tok789", "POST", "{\"commands\":[{\"code\":\"switch_1\",\"value\":true}]}",
                       "/v1.0/devices/abc/commands"),
              "EF7319EC79A0D4CABBFA9FA8B20B7B0C6C9A35A5E4414479689CD9DC1EF4FD28");
  const String pq = "/v1.0/iot-01/associated-users/devices?" +
                    TuyaClient::sortedQuery({{"size", "100"}, {"last_row_key", "k1"}}, false);
  ok &= check("tuya-list", tuyaSign("tok789", "GET", "", pq),
              "4E4AAD64B85F9E653DF9E8804D40BC581D45C325BACF54968287569755304F9D");
  Serial.printf("[selftest] %s\n", ok ? "SVE OK" : "IMA GRESAKA");
  return ok;
}

}  // namespace selftest
