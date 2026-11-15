#pragma once
#include <Arduino.h>

void setupNetwork();
void handleNetwork();
bool networkReady();
bool networkAccessPointReady();
uint8_t actualWifiChannel();
String networkStatusJson();
const char *otaUsername();
const char *otaPassword();
