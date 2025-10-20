#pragma once
#include <FS.h>

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <SPIFFS.h>
  using ServerType = WebServer;
  using Client     = WiFiClient;
#else
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <LittleFS.h>
  using ServerType = ESP8266WebServer;
  using Client     = WiFiClient;
#endif

