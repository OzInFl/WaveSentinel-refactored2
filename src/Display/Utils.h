#ifndef Utils_h
#define Utils_h

#include <lvgl.h>
#include <ui.h>

#include "Misc/Config.h"

#include "Arduino.h"

// ---------------------------------------------------------------------
// void GetEncryptionTypeString(int thisType)
// ---------------------------------------------------------------------
String GetEncryptionTypeString(int thisType) {
  switch (thisType) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA+WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2+WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    case WIFI_AUTH_MAX:
    default:
      return "Unknown";
  }
}

#endif
