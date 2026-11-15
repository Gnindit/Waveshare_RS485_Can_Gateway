#include "ota_manager.h"
#include "firmware_config.h"
#include "json_utils.h"
#include "network_manager.h"
#include "ota_policy.h"
#include "ota_public_key.h"
#include "secure_http.h"
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_timer.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

// Keep the build's board/version identity inside the signed image as well.
extern const char firmwareIdentity[] __attribute__((used)) =
    "RS485FW|waveshare-esp32-s3-rs485-can|" FIRMWARE_VERSION "|"
#if defined(GATEWAY_PUBLIC_RELEASE)
    "publicRelease|"
#endif
    ;

namespace {
struct OtaStatus {
  bool active = false;
  uint32_t downloaded = 0;
  uint32_t total = 0;
  char stage[24] = "idle";
  char error[192] = "";
};
OtaStatus status;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
bool pendingVerification = false;
const char *selfTestState = "notRequired";
uint32_t selfTestStarted = 0;
uint32_t healthySince = 0;
esp_timer_handle_t verificationTimer = nullptr;

void setStatus(const char *stage, const String &error = "", bool active = true) {
  portENTER_CRITICAL(&statusMux);
  strlcpy(status.stage, stage, sizeof(status.stage));
  strlcpy(status.error, error.c_str(), sizeof(status.error));
  status.active = active;
  portEXIT_CRITICAL(&statusMux);
}
bool downloadSmall(const String &url, std::string &body, size_t maxLength, String &error) {
  SecureHttp response;
  if (!response.open(url, error)) return false;
  if (response.length() <= 0 || response.length() > int64_t(maxLength)) { error = "Invalid manifest/signature size"; return false; }
  body.resize(response.length());
  size_t done = 0;
  const uint32_t started = millis();
  while (done < body.size()) {
    if (!networkReady() || millis() - started > 20000) { error = "Metadata download timed out or STA lost"; return false; }
    const int count = response.read(reinterpret_cast<uint8_t *>(&body[done]), body.size() - done);
    if (count <= 0) { error = "Incomplete manifest/signature download"; return false; }
    done += count;
    vTaskDelay(1);
  }
  if (!response.complete()) { error = "Metadata response incomplete"; return false; }
  return true;
}
bool verifyManifest(const std::string &body, const std::string &signature) {
  uint8_t hash[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(body.data()), body.size(), hash, 0);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  bool valid = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t *>(otaPublicKey), sizeof(otaPublicKey)) == 0;
  valid = valid && mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) && mbedtls_pk_get_bitlen(&key) == 256;
  if (valid) valid = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                        reinterpret_cast<const uint8_t *>(signature.data()), signature.size()) == 0;
  mbedtls_pk_free(&key);
  return valid;
}
bool installImage(const String &url, const OtaPolicy::Manifest &manifest,
                  const esp_partition_t *partition, String &error) {
  SecureHttp response;
  if (!response.open(url, error)) return false;
  if (response.length() != manifest.size) { error = "Firmware Content-Length differs from signed manifest"; return false; }
  esp_ota_handle_t handle = 0;
  // Incremental erase keeps flash stalls shorter than erasing the whole slot up front.
  if (esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) { error = "Cannot open inactive OTA partition"; return false; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  uint8_t buffer[4096];
  uint8_t prefix[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
  size_t prefixSize = 0;
  bool headerChecked = false;
  bool valid = true;
  uint32_t downloaded = 0;
  const uint32_t started = millis();
  setStatus("downloading");
  portENTER_CRITICAL(&statusMux);
  status.total = manifest.size;
  portEXIT_CRITICAL(&statusMux);
  while (downloaded < manifest.size) {
    if (!networkReady() || millis() - started > 180000) { error = "Firmware download timed out or STA lost"; valid = false; break; }
    const int count = response.read(buffer, std::min(sizeof(buffer), size_t(manifest.size - downloaded)));
    if (count <= 0) { error = "Firmware download interrupted"; valid = false; break; }
    if (prefixSize < sizeof(prefix)) {
      const size_t copied = std::min(size_t(count), sizeof(prefix) - prefixSize);
      memcpy(prefix + prefixSize, buffer, copied);
      prefixSize += copied;
      if (prefixSize == sizeof(prefix)) {
        esp_image_header_t image;
        esp_app_desc_t description;
        memcpy(&image, prefix, sizeof(image));
        memcpy(&description, prefix + sizeof(image) + sizeof(esp_image_segment_header_t), sizeof(description));
        if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.chip_id != ESP_CHIP_ID_ESP32S3 || description.magic_word != ESP_APP_DESC_MAGIC_WORD) {
          error = "Invalid ESP32-S3 application image (not bootloader/merged/filesystem)";
          valid = false;
          break;
        }
        headerChecked = true;
      }
    }
    if (mbedtls_sha256_update_ret(&sha, buffer, count) != 0 || esp_ota_write(handle, buffer, count) != ESP_OK) {
      error = "Firmware hash/flash write failed"; valid = false; break;
    }
    downloaded += count;
    portENTER_CRITICAL(&statusMux);
    status.downloaded = downloaded;
    portEXIT_CRITICAL(&statusMux);
    vTaskDelay(1);
  }
  setStatus("verifying");
  uint8_t hash[32];
  mbedtls_sha256_finish_ret(&sha, hash);
  mbedtls_sha256_free(&sha);
  char hashHex[65];
  for (size_t i = 0; i < sizeof(hash); ++i) snprintf(hashHex + 2 * i, 3, "%02x", hash[i]);
  if (valid && (!headerChecked || !response.complete() || manifest.sha256 != hashHex)) {
    error = "Firmware integrity check failed"; valid = false;
  }
  if (!valid) { esp_ota_abort(handle); return false; }
  // esp_ota_end checks the ESP image checksum/hash and chip revision before activation.
  if (esp_ota_end(handle) != ESP_OK) { error = "ESP image validation failed"; return false; }
  setStatus("installing");
  if (esp_ota_set_boot_partition(partition) != ESP_OK) { error = "Cannot activate verified OTA image"; return false; }
  return true;
}
void performOta(const String &url) {
  String error;
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  bool valid = partition && partition != esp_ota_get_running_partition();
  if (!valid) error = "No inactive OTA partition";
  const String base = url.substring(0, url.length() - strlen("firmware.bin"));
  std::string manifestText;
  std::string signature;
  OtaPolicy::Manifest manifest;
  if (valid) valid = downloadSmall(base + "firmware.manifest", manifestText, 1024, error);
  if (valid) valid = downloadSmall(base + "firmware.manifest.sig", signature, 128, error);
  if (valid && !verifyManifest(manifestText, signature)) { error = "Manifest signature invalid or untrusted signing key"; valid = false; }
  if (valid && !OtaPolicy::parseManifest(manifestText, FirmwareConfig::deviceId, partition->size, manifest)) {
    error = "Manifest target, size or format invalid"; valid = false;
  }
  if (valid) valid = installImage(url, manifest, partition, error);
  if (valid) {
    setStatus("rebooting");
    vTaskDelay(pdMS_TO_TICKS(2500));
    ESP.restart();
  } else {
    setStatus("error", error, false);
    if (Serial) Serial.printf("OTA failed: %s | WiFi channel %u\n", error.c_str(), actualWifiChannel());
  }
}
void otaTask(void *argument) {
  auto *url = static_cast<String *>(argument);
  performOta(*url); // Leave all C++ scopes before deleting this FreeRTOS task.
  delete url;
  vTaskDelete(nullptr);
}
}

// Arduino otherwise accepts a pending OTA before setup(), before our self-test.
extern "C" bool verifyRollbackLater() { return true; }

bool otaConfigured() { return strlen(otaPublicKey) > 0; }
bool otaBusy() {
  portENTER_CRITICAL(&statusMux);
  const bool active = status.active;
  portEXIT_CRITICAL(&statusMux);
  return active;
}
void setupOta() {
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  pendingVerification = running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  selfTestStarted = millis();
  if (pendingVerification) {
    selfTestState = "pending";
    esp_timer_create_args_t timerArgs = {};
    // Independent of loop(): a hung main loop must also reset into rollback.
    timerArgs.callback = [](void *) { esp_restart(); };
    timerArgs.name = "otaSelfTest";
    if (esp_timer_create(&timerArgs, &verificationTimer) != ESP_OK ||
        esp_timer_start_once(verificationTimer, 90000000) != ESP_OK) {
      selfTestState = "timerFailed";
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
  }
  if (Serial) Serial.printf("Image identity: %s\n", firmwareIdentity);
  if (Serial) Serial.printf("OTA signing key: %s | rollback self-test: %s\n", otaConfigured() ? "configured" : "MISSING (OTA disabled)", selfTestState);
}
void handleOtaSelfTest(bool runtimeHealthy) {
  if (!pendingVerification) return;
  if (runtimeHealthy) {
    if (!healthySince) healthySince = millis();
    if (millis() - healthySince >= 30000 && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pendingVerification = false;
      if (verificationTimer) esp_timer_stop(verificationTimer);
      selfTestState = "passed";
      if (Serial) Serial.println("OTA self-test passed; new firmware confirmed");
    }
  } else healthySince = 0;
  if (pendingVerification && millis() - selfTestStarted >= 90000) {
    selfTestState = "failed";
    if (Serial) Serial.println("OTA self-test failed; rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}
bool startOta(const String &url, String &error) {
  if (!otaConfigured()) { error = "No trusted firmware signing key configured"; return false; }
  if (pendingVerification) { error = "Wait for boot self-test to pass"; return false; }
  if (!networkReady()) { error = "STA must be connected on channel 1"; return false; }
  if (!hasValidClock()) { error = "Waiting for SNTP time; cannot verify TLS certificates"; return false; }
  if (url.length() > 512 || !isSecureUrl(url) || !url.endsWith("/firmware.bin") || url.indexOf('?') >= 0) {
    error = "Use an HTTPS URL ending in /firmware.bin without a query"; return false;
  }
  portENTER_CRITICAL(&statusMux);
  const bool busy = status.active;
  if (!busy) {
    status.active = true;
    status.downloaded = 0;
    status.total = 0;
    status.error[0] = 0;
    strlcpy(status.stage, "manifest", sizeof(status.stage));
  }
  portEXIT_CRITICAL(&statusMux);
  if (busy) { error = "An OTA update is already running"; return false; }
  auto *argument = new String(url);
  if (!argument || xTaskCreate(otaTask, "pullOta", 14336, argument, 1, nullptr) != pdPASS) {
    delete argument;
    error = "Cannot allocate OTA task";
    setStatus("error", error, false);
    return false;
  }
  return true;
}
String otaStatusJson() {
  OtaStatus snapshot;
  portENTER_CRITICAL(&statusMux);
  snapshot = status;
  portEXIT_CRITICAL(&statusMux);
  return String("{\"active\":") + (snapshot.active ? "true" : "false") + ",\"stage\":" + jsonQuote(snapshot.stage) +
      ",\"downloaded\":" + String(snapshot.downloaded) + ",\"total\":" + String(snapshot.total) +
      ",\"error\":" + jsonQuote(snapshot.error) + "}";
}
String otaSystemJson() {
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  return String("{\"configured\":") + (otaConfigured() ? "true" : "false") + ",\"busy\":" + (otaBusy() ? "true" : "false") +
      ",\"slotSize\":" + String(slot ? slot->size : 0) + ",\"selfTest\":" + jsonQuote(selfTestState) +
      ",\"rollbackSupported\":" +
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
      "true}";
#else
      "false}";
#endif
}
