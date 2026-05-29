#ifndef WiFix_h
#define WiFix_h

#include <lvgl.h>
#include <ui.h>

#include "Misc/Config.h"

#include "Arduino.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <Update.h>
#include <ArduinoOTA.h>

// Wifi Paramaters
const char *ssid = "WAVESENTINEL";
const char *password = "987654321";
const int wifi_channel = 12;

// Old WebServer removed — replaced by LocalAPI (AsyncWebServer)


// WiFi Scan State
bool scanFinished = false;

// WiFi STA Connection State (set by WiFiEvent, read by state machine)
volatile bool wifiConnected = false;
volatile bool wifiGotIP = false;
char wifiLocalIP[16] = {0};

// OTA State
int OTAInProgress = 0; // OTA Flag

// ---------------------------------------------------------------------
// void WiFiEvent(WiFiEvent_t event)
// ---------------------------------------------------------------------
void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_SCAN_DONE:
    Print_Debug("Completed scan for access points");
    scanFinished = true;
    break;
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Print_Debug("WiFi connected to AP");
    wifiConnected = true;
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Print_Debug("WiFi disconnected");
    wifiConnected = false;
    wifiGotIP = false;
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Print_Debug("WiFi got IP");
    wifiGotIP = true;
    {
      IPAddress ip = WiFi.localIP();
      snprintf(wifiLocalIP, sizeof(wifiLocalIP), "%d.%d.%d.%d",
               ip[0], ip[1], ip[2], ip[3]);
    }
    break;
  default:
    break;
  }
}

// Old handleRoot/handleUpdate removed — replaced by LocalAPI

#endif
