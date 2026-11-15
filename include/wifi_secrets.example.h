#pragma once

// Copy to wifi_secrets.h (ignored by Git). Never commit actual credentials.
// The USB/main build provisions these in NVS. Public OTA/release builds exclude this header.
namespace WifiSecrets {
constexpr const char *staSsid = "";       // Boat router, 2.4 GHz, FIXED channel 1
constexpr const char *staPassword = "";
constexpr const char *apSsid = "ESP32-S3-RS485";
constexpr const char *apPassword = "";    // 8-63 chars; empty generates a device password in NVS
constexpr const char *otaUsername = "admin";
constexpr const char *otaPassword = "";   // >=16 chars; empty generates a device password in NVS
}
