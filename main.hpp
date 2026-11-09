#pragma once
#include "Arduino.h"

enum class Location {
  FileSelecter,
  Server,
  File,
  Game
};

extern Location UserLocation;
extern void processBat(void);

extern String aiGetWifiSsid();
extern String aiGetWifiPass();
extern String aiGetApiKey();
extern String aiGetApiUrl();
extern String aiGetApiModel();
extern void   aiSetAll(const String& ssid, const String& pass,
                       const String& key,  const String& url,
                       const String& model);
extern bool   aiIsConfigured();