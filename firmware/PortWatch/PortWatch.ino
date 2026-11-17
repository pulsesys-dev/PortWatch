/*
  PortWatch — AnyPS5 progress desk display
  Pulse Systems · firmware b1.0

  Shows how far the AnyPS5 project (github.com/boykopovar/AnyPS5) has got reimplementing the
  PS5 system software: library functions, shader recompiler and image-format coverage, a
  history chart, the libraries that moved, and a tile map of every library.

  Boards (one sketch, picked automatically by the chip you compile for):
    ESP32     -> ESP32-2432S028R "Cheap Yellow Display" 2.8"  (Arduino IDE: "ESP32 Dev Module")
    ESP32-S3  -> Guition JC4827W543 4.3", C or R              (Arduino IDE: "ESP32S3 Dev Module", OPI PSRAM)

  Libraries: GFX Library for Arduino 1.4.9, ArduinoJson 7.x, XPT2046_Touchscreen
  Core: esp32 by Espressif 2.0.17 (the versions LapLogic runs on these boards)

  Touch: right third = next screen, left third = previous, middle = screen action
         (Libraries: change sort; Overview: check feed now). Hold 2 s = device info.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "board.h"
#include "feed.h"
#include "ui.h"
#include "improv.h"
#include "web.h"

uint32_t lastTouch = 0, lastCycle = 0, lastHeader = 0, lastBl = 0, wakeUntil = 0, celebUntil = 0, lastLoading = 0;
bool pressed = false, longFired = false, wasConnected = false;
uint32_t pressStart = 0; int16_t pressX = 0, pressY = 0;

// ---------- Wi-Fi ----------
String deviceUrl() { return WiFi.status() == WL_CONNECTED ? "http://" + WiFi.localIP().toString() + "/" : String(); }

bool connectWifi(const String &ssid, const String &pass, uint32_t timeoutMs) {
  if (!ssid.length()) return false;
  WiFi.mode(apMode ? WIFI_AP_STA : WIFI_STA);
  WiFi.setHostname("portwatch");
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) { delay(100); }
  return WiFi.status() == WL_CONNECTED;
}

void onConnected() {
  MDNS.end();
  if (MDNS.begin("portwatch")) MDNS.addService("http", "tcp", 80);
  configTzTime(S.tz.c_str(), "pool.ntp.org", "time.google.com");
  Serial.printf("Wi-Fi up: %s  (http://portwatch.local)\n", WiFi.localIP().toString().c_str());
  fetchNow();
}

void leaveAP() {
  apMode = false;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  onConnected();
}

// ---------- screens ----------
void stepScreen(int dir, bool autoRotate = false) {
  for (int i = 0; i < SC_COUNT; i++) {
    screen = (screen + SC_COUNT + dir) % SC_COUNT;
    if (screenEnabled(screen) && !(autoRotate && screen == SC_INFO)) break;
  }
  drawScreen();
}

void drawLoading() {
  if (apMode) {
    char l2[48]; snprintf(l2, sizeof l2, "Join Wi-Fi  %s", apName.c_str());
    messageScreen("SETUP", "Connect me to Wi-Fi", l2, "then open 192.168.4.1");
  } else if (WiFi.status() != WL_CONNECTED) {
    messageScreen("STARTING", "Joining Wi-Fi", S.ssid.c_str());
  } else if (fetchError.length() && !fetching) {
    messageScreen("FEED", "Can't load AnyPS5 data", fetchError.c_str(), "retrying every minute");
  } else {
    char l2[48];
    if (dlTotal) snprintf(l2, sizeof l2, "%lu of %lu KB", (unsigned long)(dlBytes / 1024), (unsigned long)(dlTotal / 1024));
    else snprintf(l2, sizeof l2, "%lu KB", (unsigned long)(dlBytes / 1024));
    messageScreen("LOADING", "Reading the AnyPS5 feed", dlBytes ? l2 : "connecting…", nullptr,
                  dlTotal ? (float)dlBytes / dlTotal : -1);
  }
}

// ---------- backlight schedule ----------
void updateBacklight() {
  uint8_t target = S.bright;
  if (S.nightOn && timeValid() && millis() > wakeUntil) {
    struct tm t; time_t now = time(nullptr); localtime_r(&now, &t);
    bool night = S.nightStart > S.nightEnd ? (t.tm_hour >= S.nightStart || t.tm_hour < S.nightEnd)
                                           : (t.tm_hour >= S.nightStart && t.tm_hour < S.nightEnd);
    if (night) target = S.nightBright;
  }
  blSet(target);
}

// ---------- touch ----------
// Double-tap the left or right side to change screen; a single tap there does nothing,
// so brushing the screen won't flip pages. Centre tap: Libraries = change sort, Overview = check feed now.
uint32_t lastTapAt = 0; int8_t lastTapSide = 0;
void onTap(int16_t x, int16_t y) {
  (void)y;
  if (celebUntil) { celebUntil = 0; ledSet(0, 0, 0); drawScreen(); return; }
  if (!model && screen != SC_INFO) return;
  int8_t side = x < W * 3 / 10 ? -1 : x > W * 7 / 10 ? 1 : 0;
  uint32_t now = millis();
  if (side) {
    if (side == lastTapSide && now - lastTapAt < 450) { lastTapSide = 0; stepScreen(side); }
    else { lastTapSide = side; lastTapAt = now; }
    return;
  }
  lastTapSide = 0;
  if (screen == SC_LIBS) { libMode = (libMode + 1) % 4; drawScreen(); }
  else if (screen == SC_OVERVIEW) { etag = ""; fetchNow(); }
}

// Text commands from the browser flasher over USB (lines starting "PW:")
void handleCommand(const String &line) {
  if (line == "PW:INFO") {
    Serial.printf("PW:INFO {\"name\":\"%s\",\"version\":\"%s\",\"board\":\"%s\",\"touch\":\"%s\",\"rotation\":%u,\"calibrated\":%s,\"wifi\":%s}\n",
                  EP_NAME, EP_VERSION, EP_BOARD, touchKind == TOUCH_XPT ? "resistive" : touchKind == TOUCH_GT911 ? "capacitive" : "none",
                  S.rotation, cal.magic == CAL_MAGIC ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false");
    Serial.printf("PW:INV %d\n", S.invert ? 1 : 0);
  } else if (line == "PW:CAL") {
    if (!touchIsResistive()) { Serial.println("PW:ERR not a resistive screen"); return; }
    Serial.println("PW:OK CAL started");
    calibratePending = true;
  } else if (line.startsWith("PW:INV=")) {     // invert colours, applied straight away
    S.invert = line.substring(7).toInt() != 0; saveSettings();
    panel->invertDisplay(S.invert);
    Serial.printf("PW:OK INV %d\n", S.invert ? 1 : 0);
  } else if (line.startsWith("PW:ROT=")) {
    int r = line.substring(7).toInt();
    if (r < 0 || r > 3) { Serial.println("PW:ERR rotation 0-3"); return; }
    S.rotation = r; saveSettings();
    Serial.println("PW:OK ROT restarting");
    rebootPending = true;
  }
}

void pollTouch() {
  int16_t x, y;
  bool down = touchRead(x, y);
  uint32_t now = millis();
  if (down) {
    if (!pressed) { pressed = true; longFired = false; pressStart = now; pressX = x; pressY = y; }
    if (!longFired && now - pressStart > 2000) {
      longFired = true; lastTouch = now;
      screen = SC_INFO; drawScreen();
    }
  } else if (pressed) {
    pressed = false; lastTouch = now;
    bool wasDim = blNow < S.bright;
    wakeUntil = now + 60000; updateBacklight();
    if (!longFired && now - pressStart > 30 && !wasDim) onTap(pressX, pressY);
  }
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nPortWatch " EP_VERSION " · " EP_BOARD);
  loadSettings();
  blBegin(); blSet(0);
  ledBegin();
  if (!displayBegin()) Serial.println("Display init failed");
  touchBegin();
  g->setTextWrap(false);

  // splash
  g->fillScreen(C_BG);
  txt(F_L, W / 2, H / 2 - 4, "PortWatch", C_FG, 1);
  txt(F_S, W / 2, H / 2 + 22, "AnyPS5 progress", C_ACCENT2, 1);
  if (touchIsResistive()) txt(F_TINY, W / 2, H - 14, "hold the screen to calibrate touch", C_DIM, 1);
  txt(F_TINY, W - 8, H - 4, EP_VERSION, C_DIM, 2);
  flush();
  blSet(S.bright);

  if (touchIsResistive()) {
    bool needCal = cal.magic != CAL_MAGIC, held = false;
    uint32_t t0 = millis();
    while (millis() - t0 < 2500) { int16_t a, b; if (touchRaw(a, b)) { held = true; break; } delay(20); }
    if (needCal || held) runCalibration();
  }

  moversLoad();
  cacheBegin();
  model = cacheLoad();                 // last data, so the dashboard shows straight away
  if (model) drawScreen();
  improv::connectFn = connectWifi;
  improv::urlFn = deviceUrl;
  improv::cmdFn = handleCommand;

  // Wi-Fi has to be running before the web server starts (the network stack starts with it)
  if (S.ssid.length()) {
    if (!model) drawLoading();
    if (connectWifi(S.ssid, S.pass, 20000)) onConnected();
    else { startAP(); WiFi.begin(S.ssid.c_str(), S.pass.c_str()); }   // keep trying the saved network in the background
  } else startAP();
  webBegin();
  fetchBegin();
  wasConnected = WiFi.status() == WL_CONNECTED;
  if (model) drawScreen(); else drawLoading();
}

void loop() {
  uint32_t now = millis();
  improv::loop();
  webLoop();

  if (rebootPending) { delay(800); ESP.restart(); }
  if (calibratePending) {
    calibratePending = false;
    Serial.println(runCalibration() ? "PW:CAL done" : "PW:CAL cancelled");
    if (model) drawScreen(); else drawLoading();
  }

  if (apMode && WiFi.status() == WL_CONNECTED) { leaveAP(); wasConnected = true; }
  bool conn = WiFi.status() == WL_CONNECTED;
  if (conn && !wasConnected && !apMode) onConnected();   // came back after an outage
  wasConnected = conn;

  // new data from the fetch task
  if (pendingModel) {
    Model *m = (Model *)pendingModel;
    int32_t gain = applySnapshot(m);
    delete model; model = m; pendingModel = nullptr;
    cacheSave(model);
    if (gain > 0) {
      celebUntil = now + 8000; drawCelebration(gain);
      if (S.ledOn) ledSet(0, 1, 0);
      wakeUntil = now + 60000; updateBacklight();
    } else drawScreen();
  }

  if (!model) {
    if (now - lastLoading > 700) { lastLoading = now; drawLoading(); }
  } else if (celebUntil) {
    if (S.ledOn) ledSet(0, ((now / 400) % 2) == 0, 0);
    if (now > celebUntil) { celebUntil = 0; ledSet(0, 0, 0); drawScreen(); }
  } else {
    if (S.cycleSec && now - lastTouch > 60000 && now - lastCycle > S.cycleSec * 1000UL) { lastCycle = now; stepScreen(1, true); }
    static bool wasFetching = false;
    bool justFinished = wasFetching && !fetching; wasFetching = fetching;
    if (justFinished || now - lastHeader > (fetching ? 1000UL : 30000UL)) {
      lastHeader = now;
      if (screen == SC_INFO) drawScreen(); else drawHeaderOnly();
    }
  }

  pollTouch();
  if (now - lastBl > 5000) { lastBl = now; updateBacklight(); }
  delay(5);
}
