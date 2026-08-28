#include <WebServer.h>
#include <WiFi.h>

#include "sensor_page.h"
#include "sensor_server.h"

namespace {

WebServer server(80);
const SensorReading *activeReadings = nullptr;
const SensorChannel *activeChannels = nullptr;
size_t activeChannelCount = 0;

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

void startSensorAccessPoint(Print &debugSerial, const char *ssid,
                            const char *password) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password, 1);

  debugSerial.print("Access point ready. SSID: ");
  debugSerial.println(ssid);
  debugSerial.print("Web page: http://");
  debugSerial.println(WiFi.softAPIP());
}

void setupSensorWebServer(const SensorReading *readings,
                          const SensorChannel *channels,
                          size_t channelCount) {
  activeReadings = readings;
  activeChannels = channels;
  activeChannelCount = channelCount;

  server.on("/", handleIndex);
  server.on("/api/sensors", handleSensorsApi);
  server.begin();
}

void handleSensorWebClient() {
  server.handleClient();
}
