#pragma once
#include <stdint.h>

#define FIRMWARE_VERSION "1.2.0"

namespace FirmwareConfig {
constexpr const char *version = FIRMWARE_VERSION;
constexpr const char *deviceId = "waveshare-esp32-s3-rs485-can";
constexpr const char *defaultFirmwareUrl =
    "https://github.com/Gnindit/Waveshare_RS485_Can_Gateway/releases/latest/download/firmware.bin";
// Enable the debugging access point; false uses the RUT240 for web access.
constexpr bool apEnabled = false;
constexpr uint8_t wifiChannel = 1;
}
