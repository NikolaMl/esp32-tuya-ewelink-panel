#include "util/crypto.h"

#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <esp_random.h>

namespace crypto {

static const char HEX_LOWER[] = "0123456789abcdef";
static const char HEX_UPPER[] = "0123456789ABCDEF";

static String toHex(const uint8_t* d, size_t n, bool upper) {
  const char* h = upper ? HEX_UPPER : HEX_LOWER;
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    s += h[d[i] >> 4];
    s += h[d[i] & 0x0F];
  }
  return s;
}

String sha256Hex(const String& data) {
  uint8_t out[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md(info, reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), out);
  return toHex(out, sizeof(out), false);
}

void hmacSha256(const String& key, const String& data, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, reinterpret_cast<const uint8_t*>(key.c_str()), key.length(),
                  reinterpret_cast<const uint8_t*>(data.c_str()), data.length(), out);
}

String hmacSha256Hex(const String& key, const String& data, bool upper) {
  uint8_t out[32];
  hmacSha256(key, data, out);
  return toHex(out, sizeof(out), upper);
}

String base64(const uint8_t* data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  String s;
  char* buf = static_cast<char*>(malloc(olen + 1));
  if (!buf) return s;
  if (mbedtls_base64_encode(reinterpret_cast<uint8_t*>(buf), olen + 1, &olen, data, len) == 0) {
    buf[olen] = '\0';
    s = buf;
  }
  free(buf);
  return s;
}

String hmacSha256Base64(const String& key, const String& data) {
  uint8_t out[32];
  hmacSha256(key, data, out);
  return base64(out, sizeof(out));
}

String randomAlnum(size_t len) {
  static const char CH[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String s;
  s.reserve(len);
  for (size_t i = 0; i < len; i++) s += CH[esp_random() % (sizeof(CH) - 1)];
  return s;
}

String urlEncode(const String& s) {
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += c;
    } else {
      o += '%';
      o += HEX_UPPER[(static_cast<uint8_t>(c) >> 4) & 0x0F];
      o += HEX_UPPER[static_cast<uint8_t>(c) & 0x0F];
    }
  }
  return o;
}

}  // namespace crypto
