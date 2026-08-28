#ifndef SENSOR_SERVER_H
#define SENSOR_SERVER_H

#include <Arduino.h>

enum class SensorValueMode : uint8_t {
  CurrentLoop,
  DirectScaled,
};

enum class SensorRegisterType : uint8_t {
  Input,
  Holding,
};

struct SensorChannel {
  const char *name;
  const char *unit;
  uint8_t slaveId;
  uint16_t registerAddress;
  SensorRegisterType registerType;
  SensorValueMode valueMode;
  float minMa;
  float maxMa;
  float minEngineering;
  float maxEngineering;
  float directScale;
  bool active;
  bool tentativeScaling;
};

struct SensorReading {
  const SensorChannel *channel;
  uint16_t rawValue;
  float currentMa;
  float engineeringValue;
  uint8_t modbusResult;
  bool hasValidReading;
  bool hasCurrentMa;
};

void startSensorAccessPoint(Print &debugSerial, const char *ssid,
                            const char *password);
void setupSensorWebServer(const SensorReading *readings,
                          const SensorChannel *channels, size_t channelCount);
void handleSensorWebClient();

#endif
