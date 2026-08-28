#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ModbusMaster.h>
#include <WiFi.h>

#include "sensor_server.h"

// Code working on Waveshare ESP32-S3 RS485 CAN.
// Code for ESP32-S3 Modbus RTU (RS485) example that reads sensor registers
// from a Modbus slave device and serves the readings via a web page and
// ESP-NOW broadcast. The code includes pin definitions, Modbus settings,
// and functions for reading registers, broadcasting sensor snapshots,
// and handling web server requests. It also includes setup and loop
// functions to initialize the system and continuously poll the Modbus
// channels while serving the web page.

// -------------------------------
// Pin definitions from your old code
// -------------------------------
#define GPIO_PIN_CH1 47  // CH1 control GPIO

#define TXD1 17    // UART1 TX -> RS485 DI
#define RXD1 18    // UART1 RX -> RS485 RO
#define TXD1EN 21  // RS485 direction pin (DE/RE)

#define TXD2 15  // CAN/TWAI pins from old code (not used here)
#define RXD2 16  // CAN/TWAI pins from old code (not used here)

// -------------------------------
// Wifi AP settings
// -------------------------------
static const char *WIFI_AP_SSID = "ESP32-S3-RS485";
static const char *WIFI_AP_PASSWORD = "";

// -------------------------------
// Modbus settings
// -------------------------------
static const uint32_t POLL_INTERVAL_MS = 500;
static const uint32_t ESP_NOW_SEND_INTERVAL_MS = 500;
static const uint32_t SERIAL_SUMMARY_INTERVAL_MS = 5000;
static const uint32_t MODBUS_BAUDRATE = 57600;  // Common default: 9600, 19200, 38400
static const uint8_t MODBUS_READ_RETRIES = 2;
static const uint16_t MODBUS_RETRY_DELAY_MS = 20;
static const uint16_t MODBUS_SLAVE_SWITCH_SETTLE_MS = 3;
static const uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t WIFI_AP_CHANNEL = 1;
// Keep this above the configured channel count so ESP-NOW includes all active channels.
static const uint8_t ESP_NOW_MAX_BROADCAST_SENSORS = 24;
static const bool ENABLE_SLAVE_1 = true;
static const bool ENABLE_SLAVE_2 = true;
static const bool ENABLE_SLAVE_3 = true;

struct __attribute__((packed)) EspNowSensorSnapshot {
  uint8_t active;
  uint8_t valid;
  uint8_t hasCurrentMa;
  uint8_t valueMode;
  uint8_t slaveId;
  uint16_t registerAddress;
  uint16_t rawValue;
  float engineeringValue;
};

struct __attribute__((packed)) EspNowBroadcastPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t sensorCount;
  uint16_t reserved;
  uint32_t uptimeMs;
  EspNowSensorSnapshot sensors[ESP_NOW_MAX_BROADCAST_SENSORS];
};

static const uint32_t ESP_NOW_MAGIC = 0x574E5053;  // "SPNW" little-endian
static const uint8_t ESP_NOW_VERSION = 1;

// Slave id 1: 4x4-20 mA.
// Slave id 2: 8 x PT100.
// Slave id 3: 8 x K-type.
// Fuel pressure sensor is 4-20 mA, 0-0.5 bar scale.
// Water pressure sensor is 4-20 mA, -1 to 2 bar scale.
// Fuel level is intentionally deferred to another ESP node; 0x0066 stays reserved here.
// Temperature sensors are direct scaled with a factor of 0.1 (e.g., raw value of 250 = 25.0 C).
static const SensorChannel SENSOR_CHANNELS[] = {
  //Slave id 1
  {"Water pressure", "bar", 1, 0x0064, SensorRegisterType::Input, SensorValueMode::CurrentLoop,
  4.0f, 20.0f, -1.0f, 2.0f, 0.0f, ENABLE_SLAVE_1, false},
  {"Fuel pressure", "bar", 1, 0x0065, SensorRegisterType::Input, SensorValueMode::CurrentLoop,
  4.0f, 20.0f, 0.0f, 0.5f, 0.0f, ENABLE_SLAVE_1, false},
  {"Reserved / Unused", "", 1, 0x0066, SensorRegisterType::Input, SensorValueMode::CurrentLoop,
   4.0f, 18.4f, 0.0f, 100.0f, 0.0f, false, false},
  {"Reserved / Unused", "", 1, 0x0067, SensorRegisterType::Input, SensorValueMode::CurrentLoop,
   4.0f, 20.0f, 0.0f, 0.0f, 0.0f, false, false},
  //Slave id 2
  {"Cylinder left temp", "C", 2, 0x0190, SensorRegisterType::Input, SensorValueMode::DirectScaled,
  0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_2, false},
  {"Cylinder right temp", "C", 2, 0x0191, SensorRegisterType::Input, SensorValueMode::DirectScaled,
  0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_2, false},
  {"Motor ambient temp", "C", 2, 0x0192, SensorRegisterType::Input, SensorValueMode::DirectScaled,
  0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_2, false},
  {"Air ambient temp", "C", 2, 0x0193, SensorRegisterType::Input, SensorValueMode::DirectScaled,
  0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_2, false},
  {"Reserved / Unused", "C", 2, 0x0194, SensorRegisterType::Input, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 2, 0x0195, SensorRegisterType::Input, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 2, 0x0196, SensorRegisterType::Input, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 2, 0x0197, SensorRegisterType::Input, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  //Slave id 3
  {"Exhaust gas left temp", "C", 3, 0x0000, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
    0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_3, false},
  {"Exhaust gas right temp", "C", 3, 0x0001, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
    0.0f, 0.0f, 0.0f, 0.0f, 0.1f, ENABLE_SLAVE_3, false},
  {"Reserved / Unused", "C", 3, 0x0002, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 3, 0x0003, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 3, 0x0004, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 3, 0x0005, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 3, 0x0006, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},
  {"Reserved / Unused", "C", 3, 0x0007, SensorRegisterType::Holding, SensorValueMode::DirectScaled,
   0.0f, 0.0f, 0.0f, 0.0f, 0.1f, false, false},

  };

static const size_t SENSOR_CHANNEL_COUNT =
    sizeof(SENSOR_CHANNELS) / sizeof(SENSOR_CHANNELS[0]);



// Use UART1 for RS485
HardwareSerial RS485Serial(1);
ModbusMaster node;
SensorReading lastReadings[SENSOR_CHANNEL_COUNT] = {};
unsigned long lastPollMs = 0;
unsigned long lastEspNowSendMs = 0;
bool espNowReady = false;
uint8_t activeModbusSlave = 0xFF;
uint32_t espNowSendCount = 0;
uint32_t espNowSendFailCount = 0;
uint32_t modbusReadOkCount = 0;
uint32_t modbusReadFailCount = 0;
uint32_t modbusReadFailSinceSummary = 0;
uint8_t lastModbusErrorCode = 0;
uint32_t modbusSlave1OkCount = 0;
uint32_t modbusSlave1FailCount = 0;
uint32_t modbusSlave2OkCount = 0;
uint32_t modbusSlave2FailCount = 0;
uint32_t modbusSlave3OkCount = 0;
uint32_t modbusSlave3FailCount = 0;
uint32_t pollCycleCount = 0;
uint32_t pollOverrunCount = 0;
uint32_t pollDurationMinMs = 0xFFFFFFFF;
uint32_t pollDurationMaxMs = 0;
uint32_t pollDurationTotalMs = 0;
uint32_t pollDurationSampleCount = 0;
static const bool ENABLE_SERIAL_LOG = true;
static const bool LOG_EACH_POLL = false;
unsigned long lastSerialSummaryMs = 0;

// Returns true when a USB serial host is attached so logging cannot stall the loop.
inline bool isSerialHostConnected(size_t minBytes = 1) {
  return ENABLE_SERIAL_LOG && static_cast<bool>(Serial) &&
         Serial.availableForWrite() >= static_cast<int>(minBytes);
}

// Called right before a Modbus frame is transmitted.
void preTransmission();

// Called right after a Modbus frame is transmitted.
void postTransmission();

// Tracks ESP-NOW send statistics and periodically logs transmission health.
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {

  (void)mac_addr;

  ++espNowSendCount;

  if (status != ESP_NOW_SEND_SUCCESS) {

    ++espNowSendFailCount;

  }

  if (isSerialHostConnected(64) &&
      ((espNowSendCount % 10) == 0 || status != ESP_NOW_SEND_SUCCESS)) {

    Serial.print("ESP-NOW tx count=");

    Serial.print(espNowSendCount);

    Serial.print(" fail=");

    Serial.print(espNowSendFailCount);

    Serial.print(" lastStatus=");

    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");

  }

}

// Initializes ESP-NOW in AP mode and registers the broadcast peer.
void setupEspNow() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(WIFI_AP_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    if (isSerialHostConnected(32)) {
      Serial.println("ESP-NOW init failed");
    }
    espNowReady = false;
    return;
  }

  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, ESP_NOW_BROADCAST_MAC, 6);
  peerInfo.channel = WIFI_AP_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_AP;

  if (!esp_now_is_peer_exist(ESP_NOW_BROADCAST_MAC) &&
      esp_now_add_peer(&peerInfo) != ESP_OK) {
    if (isSerialHostConnected(48)) {
      Serial.println("Failed to add ESP-NOW broadcast peer");
    }
    espNowReady = false;
    return;
  }

  espNowReady = true;
  if (isSerialHostConnected(32)) {
    Serial.println("ESP-NOW ready for broadcast");
  }
  uint8_t primaryChannel = 0;
  wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primaryChannel, &secondChannel);
  if (isSerialHostConnected(32)) {
    Serial.print("ESP-NOW channel: ");
    Serial.println(primaryChannel);
  }
}

// Converts raw current or register values into engineering units for each channel.
float convertEngineeringValue(float rawValue, const SensorChannel &channel) {
  if (channel.valueMode == SensorValueMode::DirectScaled) {
    return rawValue * channel.directScale;
  }

  if (channel.maxMa == channel.minMa) {
    return channel.minEngineering;
  }

  return channel.minEngineering +
         ((rawValue - channel.minMa) *
          (channel.maxEngineering - channel.minEngineering) /
          (channel.maxMa - channel.minMa));
}

// Reads count consecutive registers starting at startAddress for the given slave, with retry logic.
// On success, fills rawValues[0..count-1] from the Modbus response buffer.
uint8_t readBatchRegisters(uint8_t slaveId, uint16_t startAddress,
                            SensorRegisterType registerType, uint8_t count,
                            uint16_t *rawValues) {
  if (activeModbusSlave != slaveId) {
    node.begin(slaveId, RS485Serial);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
    activeModbusSlave = slaveId;
    delay(MODBUS_SLAVE_SWITCH_SETTLE_MS);
  }

  uint8_t lastResult = node.ku8MBResponseTimedOut;
  for (uint8_t attempt = 0; attempt < MODBUS_READ_RETRIES; ++attempt) {
    node.clearResponseBuffer();
    uint8_t result = registerType == SensorRegisterType::Holding
                         ? node.readHoldingRegisters(startAddress, count)
                         : node.readInputRegisters(startAddress, count);
    if (result == node.ku8MBSuccess) {
      for (uint8_t i = 0; i < count; ++i) {
        rawValues[i] = node.getResponseBuffer(i);
      }
      return result;
    }

    lastResult = result;

    delay(MODBUS_RETRY_DELAY_MS);
  }

  return lastResult;
}

// Applies a Modbus batch result to a single channel's reading state.
// Called once per channel after readBatchRegisters() to distribute results.
void applyModbusResult(size_t index, uint8_t result, uint16_t rawValue) {
  const SensorChannel &channel = SENSOR_CHANNELS[index];
  SensorReading &reading = lastReadings[index];
  reading.channel = &channel;
  reading.modbusResult = result;

  if (result == node.ku8MBSuccess) {
    ++modbusReadOkCount;
    if (channel.slaveId == 1) {
      ++modbusSlave1OkCount;
    } else if (channel.slaveId == 2) {
      ++modbusSlave2OkCount;
    } else if (channel.slaveId == 3) {
      ++modbusSlave3OkCount;
    }
    reading.rawValue = rawValue;
    reading.hasCurrentMa = channel.valueMode == SensorValueMode::CurrentLoop;
    reading.currentMa = reading.hasCurrentMa ? reading.rawValue * 0.001f : 0.0f;
    reading.engineeringValue = convertEngineeringValue(
        reading.hasCurrentMa ? reading.currentMa : reading.rawValue, channel);
    reading.hasValidReading = true;

    if (LOG_EACH_POLL && isSerialHostConnected(192)) {
      Serial.print("Read OK. ");
      Serial.print(channel.name);
      Serial.print(" (slave ");
      Serial.print(channel.slaveId);
      Serial.print(") 0x");
      Serial.print(channel.registerAddress, HEX);
      Serial.print(": raw=");
      Serial.print(reading.rawValue);
      if (reading.hasCurrentMa) {
        Serial.print(", current=");
        Serial.print(reading.currentMa, 3);
        Serial.print(" mA");
      }
      Serial.print(", value=");
      Serial.print(reading.engineeringValue, 3);
      Serial.print(" ");
      Serial.print(channel.unit);

      if (channel.tentativeScaling) {
        Serial.print(" (tentative scaling)");
      }

      if (reading.hasCurrentMa &&
          (reading.currentMa < channel.minMa || reading.currentMa > channel.maxMa)) {
        Serial.print(" (input out of 4-20 mA range)");
      }

      Serial.println();
    }
  } else {
    ++modbusReadFailCount;
    ++modbusReadFailSinceSummary;
    lastModbusErrorCode = result;
    if (channel.slaveId == 1) {
      ++modbusSlave1FailCount;
    } else if (channel.slaveId == 2) {
      ++modbusSlave2FailCount;
    } else if (channel.slaveId == 3) {
      ++modbusSlave3FailCount;
    }
    reading.hasValidReading = false;
    reading.hasCurrentMa = false;
    if (LOG_EACH_POLL && isSerialHostConnected(128)) {
      Serial.print("Modbus read failed for ");
      Serial.print(channel.name);
      Serial.print(" (slave ");
      Serial.print(channel.slaveId);
      Serial.print(")");
      Serial.print(" (0x");
      Serial.print(channel.registerAddress, HEX);
      Serial.print("). Error code: 0x");
      Serial.println(result, HEX);
    }
  }
}

// Polls all active sensor groups using batched Modbus reads (one transaction per slave).
// Inactive/reserved channels are marked invalid without hitting the bus.
void pollModbus() {
  // Mark all inactive channels so downstream code sees a clean state.
  for (size_t index = 0; index < SENSOR_CHANNEL_COUNT; ++index) {
    if (!SENSOR_CHANNELS[index].active) {
      lastReadings[index].channel = &SENSOR_CHANNELS[index];
      lastReadings[index].hasValidReading = false;
      lastReadings[index].modbusResult = 0;
    }
  }

  // Slave 1: input registers 0x0064-0x0065 → channel indices 0-1
  // (Water pressure, Fuel pressure)
  if (ENABLE_SLAVE_1) {
    uint16_t rawValues[2] = {};
    uint8_t result = readBatchRegisters(1, 0x0064, SensorRegisterType::Input, 2, rawValues);
    applyModbusResult(0, result, rawValues[0]);
    applyModbusResult(1, result, rawValues[1]);
  }

  // Slave 2: input registers 0x0190-0x0193 → channel indices 4-7
  // (Cylinder left/right temp, Motor ambient, Air ambient)
  if (ENABLE_SLAVE_2) {
    uint16_t rawValues[4] = {};
    uint8_t result = readBatchRegisters(2, 0x0190, SensorRegisterType::Input, 4, rawValues);
    applyModbusResult(4, result, rawValues[0]);
    applyModbusResult(5, result, rawValues[1]);
    applyModbusResult(6, result, rawValues[2]);
    applyModbusResult(7, result, rawValues[3]);
  }

  // Slave 3: holding registers 0x0000-0x0001 → channel indices 12-13
  // (Exhaust gas left/right temp)
  if (ENABLE_SLAVE_3) {
    uint16_t rawValues[2] = {};
    uint8_t result = readBatchRegisters(3, 0x0000, SensorRegisterType::Holding, 2, rawValues);
    applyModbusResult(12, result, rawValues[0]);
    applyModbusResult(13, result, rawValues[1]);
  }
}

// Prints a compact status line at a lower rate than sensor polling.
void printSerialSummary() {
  if (!isSerialHostConnected(96)) {
    return;
  }

  size_t validCount = 0;
  size_t activeCount = 0;
  for (size_t index = 0; index < SENSOR_CHANNEL_COUNT; ++index) {
    const SensorChannel &channel = SENSOR_CHANNELS[index];
    const SensorReading &reading = lastReadings[index];
    if (!channel.active) {
      continue;
    }

    ++activeCount;
    if (reading.hasValidReading && reading.modbusResult == node.ku8MBSuccess) {
      ++validCount;
    }
  }

  Serial.print("Summary: valid=");
  Serial.print(validCount);
  Serial.print("/");
  Serial.print(activeCount);
  Serial.print(" espnow_fail=");
  Serial.print(espNowSendFailCount);
  Serial.print(" modbus_ok=");
  Serial.print(modbusReadOkCount);
  Serial.print(" modbus_fail=");
  Serial.print(modbusReadFailCount);
  Serial.print(" recent_fail=");
  Serial.println(modbusReadFailSinceSummary);

  Serial.print("Slave 1 ok/fail=");
  Serial.print(modbusSlave1OkCount);
  Serial.print("/");
  Serial.print(modbusSlave1FailCount);
  Serial.print(" | Slave 2 ok/fail=");
  Serial.print(modbusSlave2OkCount);
  Serial.print("/");
  Serial.print(modbusSlave2FailCount);
  Serial.print(" | Slave 3 ok/fail=");
  Serial.print(modbusSlave3OkCount);
  Serial.print("/");
  Serial.println(modbusSlave3FailCount);

  // Show configured interval so it's visible when tuning.
  Serial.print("Poll interval=");
  Serial.print(POLL_INTERVAL_MS);
  Serial.print("ms | Modbus read ms min/avg/max=");
  if (pollDurationSampleCount == 0) {
    Serial.print("-/-/-");
  } else {
    const uint32_t avgMs = pollDurationTotalMs / pollDurationSampleCount;
    Serial.print(pollDurationMinMs);
    Serial.print("/");
    Serial.print(avgMs);
    Serial.print("/");
    Serial.print(pollDurationMaxMs);
    // Utilization: what fraction of the poll interval the Modbus reads consume.
    Serial.print(" (");
    Serial.print((avgMs * 100) / POLL_INTERVAL_MS);
    Serial.print("% of interval)");
  }
  Serial.print(" | cycles=");
  Serial.print(pollCycleCount);
  Serial.print(" overruns=");
  Serial.println(pollOverrunCount);

  if (modbusReadFailCount > 0) {
    Serial.print("Last Modbus error code: 0x");
    if (lastModbusErrorCode < 0x10) {
      Serial.print("0");
    }
    Serial.println(lastModbusErrorCode, HEX);
  }

  for (size_t index = 0; index < SENSOR_CHANNEL_COUNT; ++index) {
    const SensorChannel &channel = SENSOR_CHANNELS[index];
    const SensorReading &reading = lastReadings[index];
    if (!channel.active || !reading.hasValidReading ||
        reading.modbusResult != node.ku8MBSuccess) {
      continue;
    }

    Serial.print("  ");
    Serial.print(channel.name);
    Serial.print("=");
    Serial.print(reading.engineeringValue, 2);
    if (channel.unit[0] != '\0') {
      Serial.print(" ");
      Serial.print(channel.unit);
    }

    Serial.println();
  }

  modbusReadFailSinceSummary = 0;
  pollDurationMinMs = 0xFFFFFFFF;
  pollDurationMaxMs = 0;
  pollDurationTotalMs = 0;
  pollDurationSampleCount = 0;
}

// Packages current readings and broadcasts them to ESP-NOW listeners.
void broadcastSensorSnapshot() {
  if (!espNowReady) {
    return;
  }

  EspNowBroadcastPacket packet = {};
  packet.magic = ESP_NOW_MAGIC;
  packet.version = ESP_NOW_VERSION;
  packet.sensorCount = SENSOR_CHANNEL_COUNT > ESP_NOW_MAX_BROADCAST_SENSORS
                          ? ESP_NOW_MAX_BROADCAST_SENSORS
                          : SENSOR_CHANNEL_COUNT;
  packet.uptimeMs = millis();

  for (size_t index = 0; index < packet.sensorCount; ++index) {
    const SensorChannel &channel = SENSOR_CHANNELS[index];
    const SensorReading &reading = lastReadings[index];
    EspNowSensorSnapshot &snapshot = packet.sensors[index];

    snapshot.active = channel.active ? 1 : 0;
    snapshot.valid = (reading.hasValidReading && reading.modbusResult == node.ku8MBSuccess) ? 1 : 0;
    snapshot.hasCurrentMa = reading.hasCurrentMa ? 1 : 0;
    snapshot.valueMode = static_cast<uint8_t>(channel.valueMode);
    snapshot.slaveId = channel.slaveId;
    snapshot.registerAddress = channel.registerAddress;
    snapshot.rawValue = reading.rawValue;
    snapshot.engineeringValue = reading.engineeringValue;
  }

  esp_err_t status = esp_now_send(ESP_NOW_BROADCAST_MAC,
                                  reinterpret_cast<const uint8_t *>(&packet),
                                  sizeof(packet));
  if (status != ESP_OK && isSerialHostConnected(48)) {
    Serial.print("ESP-NOW send failed: ");
    Serial.println(static_cast<int>(status));
  }
}

// Switches RS485 transceiver into transmit mode before Modbus send.
void preTransmission() {
  digitalWrite(TXD1EN, HIGH);  // Enable driver: transmit mode
}

// Returns RS485 transceiver to receive mode after Modbus send.
void postTransmission() {
  digitalWrite(TXD1EN, LOW);  // Disable driver: receive mode
}

// Performs one-time hardware, Modbus, Wi-Fi AP, web server, and ESP-NOW setup.
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(1000);

  if (isSerialHostConnected(32)) {
    Serial.println();
    Serial.println("ESP32-S3 Modbus RTU (RS485) example starting...");
  }

  // If this pin selects the RS485 channel on your hardware, keep it HIGH.
  pinMode(GPIO_PIN_CH1, OUTPUT);
  digitalWrite(GPIO_PIN_CH1, HIGH);

  // Direction pin for RS485 transceiver (DE/RE).
  pinMode(TXD1EN, OUTPUT);
  digitalWrite(TXD1EN, LOW);  // Start in receive mode

  // Start UART1 on your RS485 pins.
  RS485Serial.begin(MODBUS_BAUDRATE, SERIAL_8N1, RXD1, TXD1);

  // Configure Modbus master.
  node.begin(SENSOR_CHANNELS[0].slaveId, RS485Serial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  activeModbusSlave = SENSOR_CHANNELS[0].slaveId;

  startSensorAccessPoint(Serial, WIFI_AP_SSID, WIFI_AP_PASSWORD);
  setupEspNow();
  setupSensorWebServer(lastReadings, SENSOR_CHANNELS, SENSOR_CHANNEL_COUNT);

  if (isSerialHostConnected(64)) {
    Serial.println("Setup complete. Reading sensor registers and serving web page...");
  }
}

// Serves web requests continuously and runs periodic sensor polling/broadcast.
void loop() {
  handleSensorWebClient();

  if (millis() - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = millis();
    const uint32_t pollStartMs = millis();
    pollModbus();
    const uint32_t pollDurationMs = millis() - pollStartMs;
    ++pollCycleCount;
    ++pollDurationSampleCount;
    pollDurationTotalMs += pollDurationMs;
    if (pollDurationMs < pollDurationMinMs) {
      pollDurationMinMs = pollDurationMs;
    }
    if (pollDurationMs > pollDurationMaxMs) {
      pollDurationMaxMs = pollDurationMs;
    }
    if (pollDurationMs > POLL_INTERVAL_MS) {
      ++pollOverrunCount;
    }
  }

  if (millis() - lastEspNowSendMs >= ESP_NOW_SEND_INTERVAL_MS) {
    lastEspNowSendMs = millis();
    broadcastSensorSnapshot();
  }

  if (millis() - lastSerialSummaryMs >= SERIAL_SUMMARY_INTERVAL_MS) {
    lastSerialSummaryMs = millis();
    printSerialSummary();
  }
}
