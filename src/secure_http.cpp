#include "secure_http.h"
#include "firmware_config.h"
#include "ota_policy.h"
#include <time.h>
#include <esp_tls.h>

// Arduino ships a different esp_crt_bundle.h with the same include name.
// Use the ESP-IDF API and its built-in full Mozilla CA bundle, not the Arduino shim.
extern "C" esp_err_t esp_crt_bundle_attach(void *config);

bool isSecureUrl(const String &url) {
  return OtaPolicy::secureUrl(std::string(url.c_str(), url.length()));
}

bool hasValidClock() { return time(nullptr) >= 1704067200; }

SecureHttp::~SecureHttp() { close(); }
void SecureHttp::close() {
  if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); client = nullptr; }
}
esp_err_t SecureHttp::onEvent(esp_http_client_event_t *event) {
  auto *self = static_cast<SecureHttp *>(event->user_data);
  if (event->event_id == HTTP_EVENT_ON_HEADER) {
    if (!strcasecmp(event->header_key, "Location")) self->location = event->header_value;
    if (!strcasecmp(event->header_key, "Content-Encoding")) self->encoding = event->header_value;
  } else if (event->event_id == HTTP_EVENT_ERROR && event->data) {
    esp_tls_get_and_clear_last_error(static_cast<esp_tls_error_handle_t>(event->data),
                                    &self->lastTlsError, &self->lastCertFlags);
  }
  return ESP_OK;
}
bool SecureHttp::open(const String &initialUrl, String &error, bool requireKnownLength) {
  String url = initialUrl;
  for (unsigned hop = 0; hop <= 5; ++hop) {
    close();
    if (!isSecureUrl(url)) { error = "Only HTTPS URLs on port 443 without credentials/fragments are allowed"; return false; }
    if (!hasValidClock()) { error = "Clock not synchronized; TLS requires valid time"; return false; }
    location = "";
    encoding = "";
    lastTlsError = 0;
    lastCertFlags = 0;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 5000;
    config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = onEvent;
    config.user_data = this;
    config.buffer_size = 2048;
    config.buffer_size_tx = OtaPolicy::requestBufferSize(url.length());
    config.user_agent = "Waveshare-RS485/" FIRMWARE_VERSION;
    client = esp_http_client_init(&config);
    if (!client) { error = "Cannot allocate HTTPS client"; return false; }
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    const esp_err_t result = esp_http_client_open(client, 0);
    if (result != ESP_OK) {
      const int slash = url.indexOf('/', 8);
      const String host = url.substring(8, slash < 0 ? url.length() : slash);
      char details[96];
      snprintf(details, sizeof(details), " (%s; TLS=0x%x; cert=0x%x)",
               esp_err_to_name(result), static_cast<unsigned>(lastTlsError), static_cast<unsigned>(lastCertFlags));
      // Log the hostname only, never the signed redirect URL/query.
      error = "HTTPS request failed at " + host + details;
      return false;
    }
    responseLength = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      if (location.startsWith("/") && !location.startsWith("//")) {
        const int slash = url.indexOf('/', 8);
        location = url.substring(0, slash < 0 ? url.length() : slash) + location;
      }
      if (!isSecureUrl(location)) { error = "Unsafe or missing HTTPS redirect"; return false; }
      url = location;
      continue;
    }
    if (status != 200) { error = "HTTPS returned HTTP " + String(status); return false; }
    if (!encoding.isEmpty() && encoding != "identity") { error = "Compressed HTTP response rejected"; return false; }
    if (requireKnownLength && (responseLength < 0 || esp_http_client_is_chunked_response(client))) {
      error = "Response must have a known Content-Length"; return false;
    }
    return true;
  }
  error = "Too many HTTPS redirects";
  return false;
}
int SecureHttp::read(uint8_t *buffer, size_t length) {
  return esp_http_client_read(client, reinterpret_cast<char *>(buffer), length);
}
bool SecureHttp::complete() const { return client && esp_http_client_is_complete_data_received(client); }
