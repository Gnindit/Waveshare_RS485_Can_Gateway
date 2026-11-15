#pragma once
#include <Arduino.h>

void setupOta();
void handleOtaSelfTest(bool runtimeHealthy);
bool otaBusy();
bool otaConfigured();
bool startOta(const String &url, String &error);
String otaStatusJson();
String otaSystemJson();
