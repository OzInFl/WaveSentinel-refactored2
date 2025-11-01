#ifndef WiFix_h
#define WiFix_h

#include <lvgl.h>
#include <ui.h>

#include "Misc/Config.h"

#include "Arduino.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>

// Wifi Paramaters
const char *ssid = "WAVESENTINEL";
const char *password = "987654321";
const int wifi_channel = 12;

// Web Service for Updates
WebServer server(80);


// WiFi Scan State
bool scanFinished = false;

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
  default:
    break;
  }
}

// ---------------------------------------------------------------------
// void handleRoot()
// ---------------------------------------------------------------------
void handleRoot()
{
  String html = "<html><body>";
  html += "<h1>ESP32 Firmware Update</h1>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='firmware'>";
  html += "<input type='submit' value='Upload Firmware'>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------
// void handleUpdate()
// ---------------------------------------------------------------------
void handleUpdate()
{
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    lv_label_set_text(ui_lblSettingsStatus, "Starting firmware update");
    if (!Update.begin())
    {
      lv_label_set_text(ui_lblSettingsStatus, "Update failed to begin.");
      server.send(500, "text/plain", "Update failed to begin.");
      return;
    }
    else
    {

      lv_label_set_text(ui_lblSettingsStatus, "Firmware update complete.");
      server.send(200, "text/plain", "Firmware update complete.");
    }
  }
}

#endif
