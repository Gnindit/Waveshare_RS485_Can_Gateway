#pragma once
#include <Arduino.h>

void setupNetwork();
void handleNetwork();
bool networkReady();
bool networkRuntimeReady();
uint8_t actualWifiChannel();
String networkStatusJson();
