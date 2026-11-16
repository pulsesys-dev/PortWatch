// PortWatch — AnyPS5 progress desk display
// Settings kept in flash (NVS) and edited from the device's web page.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define EP_NAME    "PortWatch"
#define EP_VERSION "b1.0"
#define EP_DEFAULT_FEED "https://boykopovar.github.io/AnyPS5/"

struct Settings {
  uint8_t  bright      = 80;    // % backlight
  bool     nightOn     = true;  // dim at night
  uint8_t  nightBright = 12;
  uint8_t  nightStart  = 22;    // hour, local
  uint8_t  nightEnd    = 7;
  uint16_t cycleSec    = 20;    // 0 = don't auto-cycle screens
  uint16_t refreshMin  = 30;    // how often to check the feed
  uint8_t  rotation    = 0;     // 0 = landscape, 1 = landscape flipped, 2 = portrait, 3 = portrait flipped
  uint8_t  cydPanel    = 0;     // CYD only: 0 = ILI9341 (1 USB), 1 = ST7789 (2 USB "CYD2USB")
  bool     invert      = false; // invert colours if the panel looks like a photo negative
  uint8_t  touchMode   = 0;     // Guition: 0 = auto-detect, 1 = capacitive (C), 2 = resistive (R)
  bool     ledOn       = true;  // CYD rear RGB LED flashes green on new progress
  String   tz          = "AEST-10";  // POSIX TZ string. Brisbane = AEST-10, Sydney = AEST-10AEDT,M10.1.0,M4.1.0/3
  String   feed        = "";    // blank = official AnyPS5 Pages site
  String   ssid, pass;
  bool     hasScreens  = true;  // reserved
  uint8_t  screenMask  = 0x1F;  // which screens are in the rotation (bit per screen)
};

Settings S;
Preferences prefs;

String feedBase() {
  String f = S.feed.length() ? S.feed : String(EP_DEFAULT_FEED);
  if (!f.endsWith("/")) f += "/";
  return f;
}

void loadSettings() {
  prefs.begin("portwatch", true);
  S.bright      = prefs.getUChar("bright", S.bright);
  S.nightOn     = prefs.getBool("nightOn", S.nightOn);
  S.nightBright = prefs.getUChar("nightBr", S.nightBright);
  S.nightStart  = prefs.getUChar("nightS", S.nightStart);
  S.nightEnd    = prefs.getUChar("nightE", S.nightEnd);
  S.cycleSec    = prefs.getUShort("cycle", S.cycleSec);
  S.refreshMin  = prefs.getUShort("refresh", S.refreshMin);
  S.rotation    = prefs.getUChar("rot", S.rotation) & 3;
  S.cydPanel    = prefs.getUChar("cydPanel", S.cydPanel);
  S.invert      = prefs.getBool("invert", S.invert);
  S.touchMode   = prefs.getUChar("touch", S.touchMode);
  S.ledOn       = prefs.getBool("led", S.ledOn);
  S.tz          = prefs.getString("tz", S.tz);
  S.feed        = prefs.getString("feed", S.feed);
  S.ssid        = prefs.getString("ssid", "");
  S.pass        = prefs.getString("pass", "");
  S.screenMask  = prefs.getUChar("screens", S.screenMask);
  prefs.end();
  if (S.refreshMin < 5) S.refreshMin = 5;
  if (!S.screenMask) S.screenMask = 0x1F;
}

void saveSettings() {
  prefs.begin("portwatch", false);
  prefs.putUChar("bright", S.bright);
  prefs.putBool("nightOn", S.nightOn);
  prefs.putUChar("nightBr", S.nightBright);
  prefs.putUChar("nightS", S.nightStart);
  prefs.putUChar("nightE", S.nightEnd);
  prefs.putUShort("cycle", S.cycleSec);
  prefs.putUShort("refresh", S.refreshMin);
  prefs.putUChar("rot", S.rotation);
  prefs.putUChar("cydPanel", S.cydPanel);
  prefs.putBool("invert", S.invert);
  prefs.putUChar("touch", S.touchMode);
  prefs.putBool("led", S.ledOn);
  prefs.putString("tz", S.tz);
  prefs.putString("feed", S.feed);
  prefs.putUChar("screens", S.screenMask);
  prefs.end();
}

void saveWifi(const String& ssid, const String& pass) {
  S.ssid = ssid; S.pass = pass;
  prefs.begin("portwatch", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}
