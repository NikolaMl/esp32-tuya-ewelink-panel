#pragma once
#include <Arduino.h>

namespace crypto {

// SHA-256 kao hex string (mala slova)
String sha256Hex(const String& data);

// HMAC-SHA256: sirovih 32 bajta
void hmacSha256(const String& key, const String& data, uint8_t out[32]);

// HMAC-SHA256 kao hex (velika ili mala slova)
String hmacSha256Hex(const String& key, const String& data, bool upper);

// HMAC-SHA256 kao base64
String hmacSha256Base64(const String& key, const String& data);

String base64(const uint8_t* data, size_t len);

// Nasumičan alfanumerički string (za nonce)
String randomAlnum(size_t len);

// URL encoding za query parametre
String urlEncode(const String& s);

}  // namespace crypto
