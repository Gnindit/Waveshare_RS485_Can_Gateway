#pragma once
#include <Arduino.h>
#include <esp_http_client.h>

bool isSecureUrl(const String &url);
bool hasValidClock();

// Owns one HTTPS response. Every redirect opens a new verified TLS connection.
class SecureHttp {
 public:
  SecureHttp() = default;
  SecureHttp(const SecureHttp &) = delete;
  SecureHttp &operator=(const SecureHttp &) = delete;
  ~SecureHttp();
  bool open(const String &url, String &error, bool requireKnownLength = true);
  int read(uint8_t *buffer, size_t length);
  int64_t length() const { return responseLength; }
  bool complete() const;
 private:
  static esp_err_t onEvent(esp_http_client_event_t *event);
  void close();
  esp_http_client_handle_t client = nullptr;
  String location;
  String encoding;
  int64_t responseLength = -1;
};
