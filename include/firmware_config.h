#pragma once
#include <stdint.h>

#define FIRMWARE_VERSION "1.1.3"

namespace FirmwareConfig {
constexpr const char *version = FIRMWARE_VERSION;
constexpr const char *deviceId = "waveshare-esp32-s3-rs485-can";
constexpr const char *defaultFirmwareUrl =
    "https://github.com/Gnindit/Waveshare_RS485_Can_Gateway/releases/latest/download/firmware.bin";
constexpr uint8_t wifiChannel = 1;
}
