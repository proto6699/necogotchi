#pragma once
#include <Arduino.h>

#define MAX_SAVED_NETS 5

void netBegin();                 // start auto-connect to saved networks
void netTick();                  // reconnect + NTP; call in loop
bool netConnected();
int savedCount();
String savedSsid(int i);
bool isSaved(const String &ssid, String *pass = nullptr);
void saveNetwork(const String &ssid, const String &pass);
void forgetAll();
bool connectNow(const String &ssid, const String &pass, uint32_t timeoutMs = 12000);

