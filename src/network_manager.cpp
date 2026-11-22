#include "network_manager.h"
#include "firmware_config.h"
#include "json_utils.h"
#include "secure_http.h"
#include "ota_manager.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_system.h>
#if defined(GATEWAY_PUBLIC_RELEASE)
// Public binaries must never contain this checkout's local credentials.
#elif __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#include "wifi_secrets.example.h"
#endif

namespace {
String accessPointPassword;
String accessPointSsid;
String stationSsid;
String stationPassword;
bool apReady = false;
bool networkInitialized = false;
bool scanning = false;
bool connecting = false;
bool wasConnected = false;
uint32_t attemptStarted = 0;
uint32_t lastAttempt = 0;
const char *staState = "notConfigured";
portMUX_TYPE internetMux = portMUX_INITIALIZER_UNLOCKED;
char internetState[24] = "unchecked";

void setInternetState(const char *state) {
  portENTER_CRITICAL(&internetMux);
  strlcpy(internetState, state, sizeof(internetState));
  portEXIT_CRITICAL(&internetMux);
}
String devicePassword(Preferences &preferences, const char *key, size_t minimumLength = 16) {
  String value = preferences.getString(key, "");
  if (value.length() < minimumLength) {
    char generated[33];
    snprintf(generated, sizeof(generated), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    value = generated;
    if (preferences.putString(key, value) == 0) return "";
  }
  return value;
}
String savedSetting(Preferences &preferences, const char *key, const char *configured,
                    const char *fallback, bool &valid) {
  if (*configured) {
    const String previous = preferences.getString(key, "");
    if (previous != configured && preferences.putString(key, configured) == 0) valid = false;
    return configured;
  }
  return preferences.getString(key, fallback);
}
void probeInternet(void *) {
  uint32_t lastProbe = millis() - 60000;
  for (;;) {
    if (!networkReady()) setInternetState("noSta");
    else if (!hasValidClock()) setInternetState("waitingForTime");
    else if (!otaBusy() && millis() - lastProbe >= 60000) {
      lastProbe = millis();
      setInternetState("checking");
      SecureHttp response;
      String error;
      // A verified HTTPS response measures reachability, not just association/DNS.
      const bool online = response.open("https://github.com/robots.txt", error, false);
      setInternetState(!networkReady() ? "noSta" : online ? "online" : "unreachable");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void restoreChannel() {
  const esp_err_t result = esp_wifi_set_channel(FirmwareConfig::wifiChannel, WIFI_SECOND_CHAN_NONE);
  if (result != ESP_OK && Serial) Serial.printf("WiFi restore channel failed: %s\n", esp_err_to_name(result));
}
}

uint8_t actualWifiChannel() {
  uint8_t channel = 0;
  wifi_second_chan_t secondary;
  esp_wifi_get_channel(&channel, &secondary);
  return channel;
}
bool networkReady() { return WiFi.status() == WL_CONNECTED && actualWifiChannel() == FirmwareConfig::wifiChannel; }
bool networkRuntimeReady() {
  return networkInitialized && (!FirmwareConfig::apEnabled || apReady) &&
      actualWifiChannel() == FirmwareConfig::wifiChannel;
}

void setupNetwork() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  // Enable RF entropy before generating any device passwords, without starting an AP.
  if (!WiFi.mode(WIFI_STA)) {
    if (Serial) Serial.println("WiFi initialization failed");
    return;
  }
  WiFi.disconnect(false, false);
  Preferences preferences;
  if (!preferences.begin("gateway-net", false)) {
    if (Serial) Serial.println("NVS credentials unavailable; network disabled");
    return;
  }
  bool settingsValid = true;
#if defined(GATEWAY_PUBLIC_RELEASE)
  // No example/header values can provision secrets in a public release.
  stationSsid = preferences.getString("staSsid", "");
  stationPassword = preferences.getString("staPassword", "");
  if (FirmwareConfig::apEnabled) {
    accessPointSsid = preferences.getString("apSsid", "ESP32-S3-RS485");
    accessPointPassword = devicePassword(preferences, "apPassword", 8);
  }
#else
  stationSsid = savedSetting(preferences, "staSsid", WifiSecrets::staSsid, "", settingsValid);
  if (*WifiSecrets::staSsid) {
    // An explicitly configured SSID also provisions an empty password for an open router.
    if (preferences.getString("staPassword", "") != WifiSecrets::staPassword) {
      preferences.putString("staPassword", WifiSecrets::staPassword);
      if (preferences.getString("staPassword", "") != WifiSecrets::staPassword) settingsValid = false;
    }
    stationPassword = WifiSecrets::staPassword;
  } else stationPassword = preferences.getString("staPassword", "");
  if (FirmwareConfig::apEnabled) {
    accessPointSsid = savedSetting(preferences, "apSsid", WifiSecrets::apSsid, "ESP32-S3-RS485", settingsValid);
    accessPointPassword = *WifiSecrets::apPassword ? savedSetting(preferences, "apPassword", WifiSecrets::apPassword, "", settingsValid) : devicePassword(preferences, "apPassword", 8);
  }
#endif
  preferences.end();
  if (!settingsValid || stationSsid.length() > 32 || stationPassword.length() > 64 ||
      (FirmwareConfig::apEnabled && (accessPointPassword.length() < 8 || accessPointPassword.length() > 63 ||
       accessPointSsid.isEmpty() || accessPointSsid.length() > 32))) {
    if (Serial) Serial.println("Invalid WiFi credentials; network disabled (see wifi_secrets.example.h)");
    return;
  }
  if (!WiFi.mode(FirmwareConfig::apEnabled ? WIFI_AP_STA : WIFI_STA)) {
    if (Serial) Serial.println("WiFi mode setup failed");
    return;
  }
  WiFi.setAutoReconnect(false); // Reconnect only after a channel-1 scan; never roam silently.
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);
  if (FirmwareConfig::apEnabled) {
    apReady = WiFi.softAP(accessPointSsid.c_str(), accessPointPassword.c_str(), FirmwareConfig::wifiChannel);
    wifi_config_t apConfig = {};
    if (apReady && esp_wifi_get_config(WIFI_IF_AP, &apConfig) == ESP_OK) {
      apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
      apReady = esp_wifi_set_config(WIFI_IF_AP, &apConfig) == ESP_OK;
    } else apReady = false;
  }
  restoreChannel();
  networkInitialized = !FirmwareConfig::apEnabled || apReady;
  if (Serial) {
    Serial.printf("Firmware %s | WiFi channel %u | AP %s\n", FirmwareConfig::version,
                  actualWifiChannel(), FirmwareConfig::apEnabled ? (apReady ? "ready" : "FAILED") : "disabled (use router STA IP)");
    if (apReady) {
      Serial.printf("AP %s at %s | password (keep private): %s\n", accessPointSsid.c_str(),
                    WiFi.softAPIP().toString().c_str(), accessPointPassword.c_str());
    }
  }
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  lastAttempt = millis() - 30000;
  staState = !stationSsid.isEmpty() ? "disconnected" : "notConfigured";
  if (xTaskCreate(probeInternet, "internetProbe", 8192, nullptr, 1, nullptr) != pdPASS) setInternetState("taskFailed");
}

void handleNetwork() {
  if (!networkInitialized) return;
  const uint32_t now = millis();
  if (actualWifiChannel() != FirmwareConfig::wifiChannel) {
    if (Serial) Serial.printf("CHANNEL CONFLICT: actual=%u required=%u; dropping STA to protect ESP-NOW\n", actualWifiChannel(), FirmwareConfig::wifiChannel);
    WiFi.disconnect(false, false);
    connecting = false;
    staState = "channelConflict";
    lastAttempt = now;
    restoreChannel();
  }
  const bool connected = networkReady();
  if (connected != wasConnected) {
    if (Serial) Serial.printf("STA %s | IP %s | WiFi channel %u\n", connected ? "connected" : "disconnected", WiFi.localIP().toString().c_str(), actualWifiChannel());
    wasConnected = connected;
    if (!connected) { setInternetState("noSta"); lastAttempt = now; }
  }
  if (connected) { connecting = false; staState = "connected"; return; }
  if (stationSsid.isEmpty()) return;
  if (connecting) {
    if (now - attemptStarted >= 12000) {
      WiFi.disconnect(false, false);
      connecting = false;
      staState = "connectionFailed";
      lastAttempt = now;
      restoreChannel();
    }
    return;
  }
  if (scanning) {
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    scanning = false;
    for (int i = 0; i < count; ++i) {
      if (WiFi.SSID(i) == stationSsid && WiFi.channel(i) == FirmwareConfig::wifiChannel) {
        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(i), sizeof(bssid));
        WiFi.scanDelete();
        WiFi.begin(stationSsid.c_str(), stationPassword.c_str(), FirmwareConfig::wifiChannel, bssid);
        connecting = true;
        attemptStarted = now;
        staState = "connecting";
        return;
      }
    }
    WiFi.scanDelete();
    staState = "routerNotOnChannel1";
    if (Serial) Serial.println("Router unavailable on channel 1; keeping ESP-NOW on channel 1");
    return;
  }
  if (now - lastAttempt >= 30000 && !otaBusy()) {
    lastAttempt = now;
    const int result = WiFi.scanNetworks(true, false, false, 120, FirmwareConfig::wifiChannel, stationSsid.c_str());
    scanning = result == WIFI_SCAN_RUNNING;
    staState = scanning ? "scanningChannel1" : "scanFailed";
  }
}

String networkStatusJson() {
  char internet[24];
  portENTER_CRITICAL(&internetMux);
  strlcpy(internet, internetState, sizeof(internet));
  portEXIT_CRITICAL(&internetMux);
  return String("{\"staConnected\":") + (networkReady() ? "true" : "false") +
      ",\"staState\":" + jsonQuote(staState) + ",\"staIp\":" + jsonQuote(WiFi.localIP().toString()) +
      ",\"apIp\":" + jsonQuote(apReady ? WiFi.softAPIP().toString() : String("")) + ",\"apReady\":" + (apReady ? "true" : "false") +
      ",\"apEnabled\":" + (FirmwareConfig::apEnabled ? "true" : "false") +
      ",\"rssi\":" + (networkReady() ? String(WiFi.RSSI()) : "null") +
      ",\"wifiChannel\":" + String(actualWifiChannel()) + ",\"requiredChannel\":1,\"internetStatus\":" + jsonQuote(internet) +
      ",\"clockSynchronized\":" + (hasValidClock() ? "true" : "false") + "}";
}
