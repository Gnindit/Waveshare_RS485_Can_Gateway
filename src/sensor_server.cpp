#include <WebServer.h>
#include <WiFi.h>

#include "sensor_page.h"
#include "sensor_server.h"
#include "firmware_config.h"
#include "json_utils.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "ota_policy.h"
#include <esp_system.h>

namespace {

WebServer server(80);
const SensorReading *activeReadings = nullptr;
const SensorChannel *activeChannels = nullptr;
size_t activeChannelCount = 0;
String csrfToken;
uint32_t runningFirmwareSize = 0;
uint32_t webLastHandleMs = 0;
uint32_t webMaxHandleMs = 0;

void securityHeaders() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Content-Security-Policy", "frame-ancestors 'none'");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
}

bool trustedHost() {
  const String host = server.hostHeader();
  const String ap = FirmwareConfig::apEnabled ? WiFi.softAPIP().toString() : String("");
  const String sta = WiFi.localIP().toString();
  return OtaPolicy::trustedHost(host.c_str(), ap.c_str(), sta.c_str(), WiFi.status() == WL_CONNECTED);
}

bool requireOtaAccess() {
  securityHeaders();
  if (!trustedHost()) { server.send(403, "application/json", "{\"error\":\"Use the device IP address\"}"); return false; }
  return true;
}

void handleSystem() {
  securityHeaders();
  String json = "{\"firmwareVersion\":" + jsonQuote(FirmwareConfig::version) +
      ",\"deviceId\":" + jsonQuote(FirmwareConfig::deviceId) +
      ",\"defaultFirmwareUrl\":" + jsonQuote(FirmwareConfig::defaultFirmwareUrl) +
      ",\"uptimeMs\":" + String(millis()) + ",\"freeHeap\":" + String(ESP.getFreeHeap()) +
      ",\"firmwareSize\":" + String(runningFirmwareSize) +
      ",\"webLastHandleMs\":" + String(webLastHandleMs) +
      ",\"webMaxHandleMs\":" + String(webMaxHandleMs) +
      ",\"network\":" + networkStatusJson() + ",\"ota\":" + otaSystemJson() + "}";
  server.send(200, "application/json", json);
}

void handleOtaStatus() {
  if (!requireOtaAccess()) return;
  String json = otaStatusJson();
  json.remove(json.length() - 1);
  json += ",\"csrfToken\":" + jsonQuote(csrfToken) + "}";
  server.send(200, "application/json", json);
}

void handleOtaStart() {
  if (!requireOtaAccess()) return;
  const String origin = server.header("Origin");
  const String fetchSite = server.header("Sec-Fetch-Site");
  if (!OtaPolicy::validStartRequest(server.hostHeader().c_str(), origin.c_str(), fetchSite.c_str(),
      server.header("X-CSRF-Token").c_str(), csrfToken.c_str(), server.arg("confirmation").c_str())) {
    server.send(403, "application/json", "{\"error\":\"Origin, CSRF token or confirmation invalid\"}");
    return;
  }
  String error;
  const bool wasBusy = otaBusy();
  if (!startOta(server.arg("firmwareUrl"), error)) {
    server.send(wasBusy ? 409 : 400, "application/json", "{\"error\":" + jsonQuote(error) + "}");
    return;
  }
  server.send(202, "application/json", "{\"accepted\":true}");
}


float currentToEngineeringValue(float currentMa, const SensorChannel &channel) {
  if (channel.maxMa == channel.minMa) {
    return channel.minEngineering;
  }

  return channel.minEngineering +
         ((currentMa - channel.minMa) *
          (channel.maxEngineering - channel.minEngineering) /
          (channel.maxMa - channel.minMa));
}

String sensorStatusText(const SensorReading &reading) {
  const SensorChannel *channel = reading.channel;

  if (channel != nullptr && !channel->active) {
    return "Reserved / not used";
  }

  if (!reading.hasValidReading) {
    return "Waiting for first Modbus reading";
  }

  if (reading.modbusResult != 0) {
    return "Modbus read failed. Error code: 0x" + String(reading.modbusResult, HEX);
  }

  String status = "Live reading from register 0x" +
                  String(channel != nullptr ? channel->registerAddress : 0, HEX);

  if (channel != nullptr && channel->valueMode == SensorValueMode::CurrentLoop &&
      (reading.currentMa < channel->minMa || reading.currentMa > channel->maxMa)) {
    status += " (input out of 4-20 mA range)";
  }

  if (channel != nullptr && channel->tentativeScaling) {
    status += " (tentative scaling)";
  }

  return status;
}

void handleIndex() {
  securityHeaders();
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleSensorsApi() {
  String json = "{";
  json += "\"count\":" + String(activeChannelCount);
  json += ",\"sensors\":[";

  for (size_t index = 0; index < activeChannelCount; ++index) {
    const SensorChannel &channel = activeChannels[index];
    const SensorReading &reading = activeReadings[index];
    const bool isValid = reading.hasValidReading && reading.modbusResult == 0;

    if (index > 0) {
      json += ",";
    }

    json += "{";
    json += "\"name\":\"" + String(channel.name) + "\"";
    json += ",\"unit\":\"" + String(channel.unit) + "\"";
    json += ",\"address\":" + String(channel.registerAddress);
    json += ",\"raw\":" + String(isValid ? reading.rawValue : 0);
    json += ",\"has_current\":" + String(isValid && reading.hasCurrentMa ? "true" : "false");
    json += ",\"current_ma\":" + String(isValid ? reading.currentMa : 0.0f, 3);
    json += ",\"engineering_value\":" +
            String(isValid ? reading.engineeringValue : 0.0f, 3);
    json += ",\"valid\":" + String(isValid ? "true" : "false");
    json += ",\"active\":" + String(channel.active ? "true" : "false");
    json += ",\"tentative\":" + String(channel.tentativeScaling ? "true" : "false");
    json += ",\"status\":\"" + sensorStatusText(reading) + "\"";
    json += "}";
  }

  json += "]}";
  server.send(200, "application/json", json);
}

}  // namespace

void setupSensorWebServer(const SensorReading *readings,
                          const SensorChannel *channels,
                          size_t channelCount) {
  activeReadings = readings;
  activeChannels = channels;
  activeChannelCount = channelCount;
  // Arduino getSketchSize() verifies the whole flash image. Do it once at boot,
  // not on every /api/system request in the sensor/web loop.
  runningFirmwareSize = ESP.getSketchSize();

  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
  csrfToken = token;
  const char *headers[] = {"Origin", "X-CSRF-Token", "Sec-Fetch-Site"};
  server.collectHeaders(headers, 3);
  server.on("/api/system", HTTP_GET, handleSystem);
  server.on("/api/ota/status", HTTP_GET, handleOtaStatus);
  server.on("/api/ota/start", HTTP_POST, handleOtaStart);
  server.on("/ota/login", HTTP_GET, []() {
    if (!requireOtaAccess()) return;
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/", handleIndex);
  server.on("/api/sensors", handleSensorsApi);
  server.begin();
}

void handleSensorWebClient() {
  const uint32_t started = millis();
  server.handleClient();
  webLastHandleMs = millis() - started;
  if (webLastHandleMs > webMaxHandleMs) webMaxHandleMs = webLastHandleMs;
  if (webLastHandleMs >= 250 && Serial && Serial.availableForWrite() >= 96) {
    Serial.printf("Web handling slow: %lu ms (max %lu ms)\n",
                  static_cast<unsigned long>(webLastHandleMs), static_cast<unsigned long>(webMaxHandleMs));
  }
}
