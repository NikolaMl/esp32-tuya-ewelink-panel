#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#include <utility>
#include <vector>

using HeaderList = std::vector<std::pair<String, String>>;

struct HttpResult {
  int status = 0;     // HTTP status ili negativan HTTPClient kod greške
  String body;
  bool ok() const { return status >= 200 && status < 300; }
};

// Jedna HTTPS sesija prema jednom cloudu. Veza ostaje otvorena (keep-alive)
// da komande ne bi čekale novi TLS handshake (~1 s na ESP32).
// Sertifikat servera se proverava preko ugrađenog ESP-IDF root CA paketa.
class HttpsSession {
 public:
  explicit HttpsSession(const char* tag) : tag_(tag) {}
  ~HttpsSession() { reset(); }
  HttpsSession(const HttpsSession&) = delete;
  HttpsSession& operator=(const HttpsSession&) = delete;
  HttpResult request(const char* method, const String& url, const String& body, const HeaderList& headers);
  void reset();

 private:
  HttpResult once(const char* method, const String& url, const String& body, const HeaderList& headers);
  void ensureClient();

  const char* tag_;
  // Oba objekta se prave i brišu zajedno: HTTPClient čuva pokazivač na vezu i posle end() ako je
  // veza već zatvorena, pa bi brisanje samo veze ostavilo viseći pokazivač (pad u HTTPClient::connected()).
  NetworkClientSecure* client_ = nullptr;
  HTTPClient* http_ = nullptr;
};
