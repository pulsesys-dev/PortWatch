// Wi-Fi setup portal, settings page and over-the-air updates.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "config.h"
#include "board.h"
#include "feed.h"
#include "ui.h"

WebServer server(80);
DNSServer dns;
bool apMode = false;
String apName;
bool rebootPending = false;
bool calibratePending = false;

const char PAGE_HEAD[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PortWatch</title><style>
body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:#05070d;color:#e8eefc;margin:0;padding:18px;max-width:560px;margin:auto}
h1{font-size:22px;margin:4px 0 2px}h1 span{color:#7fb0ff;font-weight:500}.sub{color:#8190b4;font-size:13px;margin-bottom:16px}
.card{background:#0b1120;border:1px solid #1b2745;border-radius:14px;padding:14px 16px;margin:12px 0}
.card h2{font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:#8190b4;margin:0 0 8px}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:7px 0;font-size:14px;border-bottom:1px solid #121b33}
label:last-child{border:0}input,select{background:#0a1328;color:#e8eefc;border:1px solid #1b2745;border-radius:8px;padding:6px 8px;font:inherit;max-width:60%}
input[type=checkbox]{width:20px;height:20px}input[type=range]{width:55%}
button,.btn{background:#2f7bff;color:#fff;border:0;border-radius:999px;padding:9px 18px;font:inherit;font-weight:600;cursor:pointer;text-decoration:none;display:inline-block}
.ghost{background:none;border:1px solid #1b2745;color:#8190b4}.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:13px}.kv b{font-weight:500;color:#8190b4}
.note{color:#4c5a7d;font-size:12px}a{color:#7fb0ff}</style></head><body>
<h1>PortWatch <span>· AnyPS5</span></h1>)HTML";

String esc(const String &s) { String o; for (char c : s) { if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else if (c == '&') o += "&amp;"; else o += c; } return o; }
String opt(int v, int cur, const char *label) { return "<option value='" + String(v) + "'" + (v == cur ? " selected" : "") + ">" + label + "</option>"; }
String chk(const char *name, bool on) { return String("<input type=checkbox name=") + name + (on ? " checked" : "") + ">"; }
String num(const char *name, int v, int lo, int hi) { return String("<input type=number name=") + name + " value=" + v + " min=" + lo + " max=" + hi + ">"; }

void pageSetup() {   // captive portal page for Wi-Fi
  String h = FPSTR(PAGE_HEAD);
  h += "<div class=sub>Connect this display to your Wi-Fi</div><div class=card><h2>Wi-Fi</h2><form method=post action=/wifi>";
  int n = WiFi.scanNetworks();
  h += "<label>Network <select name=ssid>";
  for (int i = 0; i < n; i++) { String s = WiFi.SSID(i); if (s.length()) h += "<option>" + esc(s) + "</option>"; }
  h += "</select></label><label>Or type it <input name=ssid2 placeholder='hidden network'></label>";
  h += "<label>Password <input type=password name=pass></label><div class=row><button>Connect</button></div></form></div>";
  h += "<p class=note>The display restarts and joins your network. Its settings page is then at <b>http://portwatch.local</b> or the address shown on screen.</p></body></html>";
  WiFi.scanDelete();
  server.send(200, "text/html", h);
}

void pageSettings() {
  String h = FPSTR(PAGE_HEAD);
  h += "<div class=sub>" EP_BOARD " · firmware " EP_VERSION "</div>";
  h += "<div class=card><h2>Status</h2><div class=kv>";
  if (model) {
    char a[16], b[16]; fmtNum(model->done, a); fmtNum(model->total, b);
    h += "<b>Implemented</b><span>" + String(a) + " / " + b + " (" + String(model->percent, 2) + "%)</span>";
  }
  h += "<b>Last check</b><span>" + (lastFetchOk ? agoText((millis() - lastFetchOk) / 1000) : String("not yet")) + "</span>";
  if (fetchError.length()) h += "<b>Error</b><span style='color:#ff5d6c'>" + esc(fetchError) + "</span>";
  h += "<b>Wi-Fi</b><span>" + esc(WiFi.SSID()) + " (" + WiFi.RSSI() + " dBm)</span>";
  h += "<b>Touch</b><span>" + String(touchName()) + "</span>";
  h += "<b>Free memory</b><span>" + String(ESP.getFreeHeap() / 1024) + " KB</span></div>";
  h += "<div class=row><a class='btn ghost' href=/refresh>Check feed now</a></div></div>";

  h += "<form method=post action=/save><div class=card><h2>Display</h2>";
  h += "<label>Brightness <input type=range name=bright min=5 max=100 value=" + String(S.bright) + "></label>";
  h += "<label>Dim at night " + chk("nightOn", S.nightOn) + "</label>";
  h += "<label>Night brightness <input type=range name=nightBr min=0 max=60 value=" + String(S.nightBright) + "></label>";
  h += "<label>Night from (hour) " + num("nightS", S.nightStart, 0, 23) + "</label>";
  h += "<label>Night until (hour) " + num("nightE", S.nightEnd, 0, 23) + "</label>";
  h += "<label>Change screen every <select name=cycle>" + opt(0, S.cycleSec, "Never (tap to change)") + opt(10, S.cycleSec, "10 s") + opt(20, S.cycleSec, "20 s") + opt(30, S.cycleSec, "30 s") + opt(60, S.cycleSec, "1 min") + "</select></label>";
  h += "<label>Orientation <select name=rot>" + opt(0, S.rotation, "Landscape") + opt(1, S.rotation, "Landscape, flipped") + opt(2, S.rotation, "Portrait") + opt(3, S.rotation, "Portrait, flipped") + "</select></label>";
  h += "</div><div class=card><h2>Screens in rotation</h2>";
  for (int i = 0; i < SC_COUNT; i++) h += "<label>" + String(screenNames[i]) + " " + chk((String("sc") + i).c_str(), S.screenMask & (1 << i)) + "</label>";
  h += "</div><div class=card><h2>Feed</h2>";
  h += "<label>Check every <select name=refresh>" + opt(10, S.refreshMin, "10 min") + opt(30, S.refreshMin, "30 min") + opt(60, S.refreshMin, "1 hour") + opt(180, S.refreshMin, "3 hours") + "</select></label>";
  h += "<label>Time zone (POSIX) <input name=tz value='" + esc(S.tz) + "'></label>";
  h += "<label>Feed address <input name=feed placeholder='official site' value='" + esc(S.feed) + "'></label>";
  h += "</div><div class=card><h2>Hardware</h2>";
#if EP_CYD
  h += "<label>Panel <select name=cydPanel>" + opt(0, S.cydPanel, "ILI9341 (1 USB port)") + opt(1, S.cydPanel, "ST7789 (2 USB ports)") + "</select></label>";
  h += "<label>Rear LED flashes on new progress " + chk("led", S.ledOn) + "</label>";
#else
  h += "<label>Touch <select name=touch>" + opt(0, S.touchMode, "Auto-detect") + opt(1, S.touchMode, "Capacitive (C)") + opt(2, S.touchMode, "Resistive (R)") + "</select></label>";
#endif
  h += "<label>Invert colours " + chk("invert", S.invert) + "</label>";
  h += "</div><div class=row><button>Save</button></div></form>";

  h += "<div class=card><h2>Maintenance</h2><div class=row>";
  if (touchIsResistive()) h += "<a class='btn ghost' href=/calibrate>Recalibrate touch</a>";
  h += "<a class='btn ghost' href=/forget onclick=\"return confirm('Forget Wi-Fi and restart in setup mode?')\">Forget Wi-Fi</a>";
  h += "<a class='btn ghost' href=/reboot>Restart</a></div>";
  h += "<form method=post action=/update enctype=multipart/form-data style='margin-top:14px'><label>Firmware update (.bin) <input type=file name=fw accept='.bin'></label><div class=row><button>Upload</button></div></form>";
  h += "<p class=note>Use the app-only <b>PortWatch-*.ota.bin</b> file here, or a merged .bin with the browser flasher over USB.</p></div>";
  h += "<div class=card><h2>Credits</h2><p class=note style='margin:0'>Data from the AnyPS5 project. Counts measure source reimplementation, not which games run.</p>"
       "<div class=row><a class='btn ghost' href='https://boykopovar.github.io/AnyPS5/progress.html' target=_blank rel=noopener>AnyPS5 progress page</a>"
       "<a class='btn ghost' href='https://github.com/boykopovar/AnyPS5' target=_blank rel=noopener>AnyPS5 on GitHub</a></div></div></body></html>";
  server.send(200, "text/html", h);
}

void redirectHome(const char *msg = nullptr) {
  String h = FPSTR(PAGE_HEAD);
  h += String("<div class=card>") + (msg ? msg : "Saved.") + "</div><script>setTimeout(()=>location='/',2500)</script></body></html>";
  server.send(200, "text/html", h);
}

void handleSave() {
  auto argi = [](const char *n, int d) -> int { return server.hasArg(n) ? (int)server.arg(n).toInt() : d; };
  uint8_t oldRot = S.rotation, oldPanel = S.cydPanel, oldTouch = S.touchMode; bool oldInv = S.invert;
  S.bright = constrain(argi("bright", S.bright), 5, 100);
  S.nightOn = server.hasArg("nightOn");
  S.nightBright = constrain(argi("nightBr", S.nightBright), 0, 60);
  S.nightStart = constrain(argi("nightS", S.nightStart), 0, 23);
  S.nightEnd = constrain(argi("nightE", S.nightEnd), 0, 23);
  S.cycleSec = argi("cycle", S.cycleSec);
  S.rotation = constrain(argi("rot", S.rotation), 0, 3);
  S.refreshMin = max(5, argi("refresh", S.refreshMin));
  uint8_t mask = 0; for (int i = 0; i < SC_COUNT; i++) if (server.hasArg(String("sc") + i)) mask |= 1 << i;
  S.screenMask = mask ? mask : 1;
  if (server.hasArg("tz") && server.arg("tz").length()) S.tz = server.arg("tz");
  String newFeed = server.arg("feed"); newFeed.trim();
  bool feedChanged = newFeed != S.feed; S.feed = newFeed;
#if EP_CYD
  S.cydPanel = argi("cydPanel", S.cydPanel) ? 1 : 0;
  S.ledOn = server.hasArg("led");
#else
  S.touchMode = constrain(argi("touch", S.touchMode), 0, 2);
#endif
  S.invert = server.hasArg("invert");
  saveSettings();
  panel->invertDisplay(S.invert);
  configTzTime(S.tz.c_str(), "pool.ntp.org", "time.google.com");
  if (feedChanged) { etag = ""; fetchNow(); }
  (void)oldInv;
  if (S.rotation != oldRot || S.cydPanel != oldPanel || S.touchMode != oldTouch) {
    redirectHome("Saved. Restarting to apply the display change…"); rebootPending = true;
  } else redirectHome();
  if (!(S.screenMask & (1 << screen))) screen = SC_OVERVIEW;
}

void handleWifi() {
  String ssid = server.arg("ssid2").length() ? server.arg("ssid2") : server.arg("ssid");
  if (!ssid.length()) { redirectHome("Pick a network first."); return; }
  saveWifi(ssid, server.arg("pass"));
  String h = FPSTR(PAGE_HEAD);
  h += "<div class=card>Saved. The display is restarting and joining <b>" + esc(ssid) + "</b>. Reconnect your phone to your normal Wi-Fi, then open <b>http://portwatch.local</b>.</div></body></html>";
  server.send(200, "text/html", h);
  rebootPending = true;
}

void webBegin() {
  server.on("/", HTTP_GET, [] { if (apMode) pageSetup(); else pageSettings(); });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/refresh", HTTP_GET, [] { etag = ""; fetchNow(); redirectHome("Checking the feed now…"); });
  server.on("/reboot", HTTP_GET, [] { redirectHome("Restarting…"); rebootPending = true; });
  server.on("/calibrate", HTTP_GET, [] { calibratePending = true; redirectHome("Follow the crosses on the display."); });
  server.on("/forget", HTTP_GET, [] { saveWifi("", ""); redirectHome("Wi-Fi forgotten. Restarting in setup mode…"); rebootPending = true; });
  server.on("/api/status", HTTP_GET, [] {
    String j = "{\"version\":\"" EP_VERSION "\",\"board\":\"" EP_BOARD "\"";
    if (model) j += ",\"done\":" + String(model->done) + ",\"total\":" + String(model->total) + ",\"percent\":" + String(model->percent, 2);
    j += ",\"error\":\"" + esc(fetchError) + "\",\"heap\":" + String(ESP.getFreeHeap()) + "}";
    server.send(200, "application/json", j);
  });
  server.on("/update", HTTP_POST,
    [] { bool ok = !Update.hasError(); redirectHome(ok ? "Update installed. Restarting…" : "Update failed — check you picked the .ota.bin for this board."); if (ok) rebootPending = true; },
    [] {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) { Serial.printf("OTA: %s\n", up.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
      else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
      else if (up.status == UPLOAD_FILE_END) Update.end(true);
    });
  server.onNotFound([] {
    if (apMode) { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", ""); }
    else server.send(404, "text/plain", "Not found");
  });
  server.begin();
}

void startAP() {
  apMode = true;
  uint8_t mac[6]; WiFi.macAddress(mac);
  char n[24]; snprintf(n, sizeof n, "PortWatch-%02X%02X", mac[4], mac[5]); apName = n;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str());
  dns.start(53, "*", WiFi.softAPIP());
}

void webLoop() {
  server.handleClient();
  if (apMode) dns.processNextRequest();
}
