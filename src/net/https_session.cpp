#include "net/https_session.h"

// Root CA paket koji je ugrađen u ESP-IDF mbedtls biblioteku (ista lista kao Mozilla NSS).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

void HttpsSession::ensureClient() {
  if (client_ && http_) return;
  reset();
  client_ = new NetworkClientSecure();
  client_->setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  client_->setTimeout(12);  // sekunde
  http_ = new HTTPClient();
  http_->setReuse(true);
  http_->setTimeout(12000);
  http_->setConnectTimeout(10000);
  http_->useHTTP10(false);
}

void HttpsSession::reset() {
  // Prvo HTTPClient (njegov destruktor još sme da koristi vezu), pa tek onda veza
  if (http_) {
    http_->end();
    delete http_;
    http_ = nullptr;
  }
  if (client_) {
    client_->stop();
    delete client_;
    client_ = nullptr;
  }
}

HttpResult HttpsSession::once(const char* method, const String& url, const String& body, const HeaderList& headers) {
  HttpResult r;
  ensureClient();
  if (!http_->begin(*client_, url)) {
    r.status = HTTPC_ERROR_CONNECTION_REFUSED;
    return r;
  }
  for (const auto& h : headers) http_->addHeader(h.first, h.second);
  r.status = http_->sendRequest(method, body);
  if (r.status > 0) r.body = http_->getString();
  http_->end();
  return r;
}

HttpResult HttpsSession::request(const char* method, const String& url, const String& body,
                                 const HeaderList& headers) {
  const uint32_t t0 = millis();
  HttpResult r = once(method, url, body, headers);
  if (r.status < 0) {
    // Server je verovatno zatvorio keep-alive vezu — otvori novu i probaj još jednom
    log_w("[%s] %s %s -> %d (%s), ponavljam", tag_, method, url.c_str(), r.status,
          HTTPClient::errorToString(r.status).c_str());
    reset();
    r = once(method, url, body, headers);
  }
  log_i("[%s] %s %s -> %d (%u ms, %u B)", tag_, method, url.c_str(), r.status, (unsigned)(millis() - t0),
        (unsigned)r.body.length());
  if (r.status < 0) reset();
  return r;
}
