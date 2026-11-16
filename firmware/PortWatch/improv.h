// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/)
// Lets the browser flasher (or ESP Web Tools) send Wi-Fi details straight after flashing.
#pragma once
#include <WiFi.h>
#include "config.h"
#include "board.h"

namespace improv {
enum : uint8_t { T_STATE = 1, T_ERROR = 2, T_RPC = 3, T_RPC_RESULT = 4 };
enum : uint8_t { S_READY = 2, S_PROVISIONING = 3, S_PROVISIONED = 4 };
enum : uint8_t { E_NONE = 0, E_INVALID = 1, E_UNKNOWN_CMD = 2, E_CONNECT = 3, E_OTHER = 0xFF };
enum : uint8_t { C_WIFI = 1, C_STATE = 2, C_INFO = 3, C_SCAN = 4 };

uint8_t rx[256]; size_t rxLen = 0;
bool (*connectFn)(const String &, const String &, uint32_t) = nullptr;   // set by the sketch
String (*urlFn)() = nullptr;
void (*cmdFn)(const String &) = nullptr;   // plain-text "PW:..." lines
String line;

void send(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t hdr[9] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, len};
  uint8_t sum = 0;
  for (uint8_t b : hdr) sum += b;
  for (uint8_t i = 0; i < len; i++) sum += data[i];
  Serial.write(hdr, 9); if (len) Serial.write(data, len); Serial.write(sum); Serial.write('\n');
  Serial.flush();
}
void sendState(uint8_t s) { send(T_STATE, &s, 1); }
void sendError(uint8_t e) { send(T_ERROR, &e, 1); }
void sendResult(uint8_t cmd, const String *strs, uint8_t n) {
  uint8_t buf[250]; uint8_t p = 2;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t l = min((size_t)strs[i].length(), (size_t)(240 - p)); buf[p++] = l;
    memcpy(buf + p, strs[i].c_str(), l); p += l;
  }
  buf[0] = cmd; buf[1] = p - 2;
  send(T_RPC_RESULT, buf, p);
}
uint8_t currentState() { return WiFi.status() == WL_CONNECTED ? S_PROVISIONED : S_READY; }

void handle(const uint8_t *d, uint8_t len) {
  if (len < 2) { sendError(E_INVALID); return; }
  uint8_t cmd = d[0], dl = d[1];
  if (dl + 2 > len) { sendError(E_INVALID); return; }
  const uint8_t *p = d + 2;
  switch (cmd) {
    case C_WIFI: {
      uint8_t sl = p[0];
      if (sl + 2 > dl) { sendError(E_INVALID); return; }
      uint8_t pl = p[1 + sl];
      if (sl + pl + 2 > dl) { sendError(E_INVALID); return; }
      String ssid, pass;
      for (uint8_t i = 0; i < sl; i++) ssid += (char)p[1 + i];
      for (uint8_t i = 0; i < pl; i++) pass += (char)p[2 + sl + i];
      sendState(S_PROVISIONING);
      if (connectFn && connectFn(ssid, pass, 20000)) {
        saveWifi(ssid, pass);
        sendState(S_PROVISIONED);
        String url = urlFn ? urlFn() : String();
        sendResult(C_WIFI, &url, url.length() ? 1 : 0);
      } else {
        sendError(E_CONNECT);
        sendState(S_READY);
      }
      break;
    }
    case C_STATE: {
      uint8_t s = currentState(); sendState(s);
      if (s == S_PROVISIONED && urlFn) { String url = urlFn(); sendResult(C_STATE, &url, 1); }
      break;
    }
    case C_INFO: {
      String info[4] = {EP_NAME, EP_VERSION,
#if EP_GUITION
                        "ESP32-S3",
#else
                        "ESP32",
#endif
                        EP_BOARD};
      sendResult(C_INFO, info, 4);
      break;
    }
    case C_SCAN: {
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n && i < 20; i++) {
        String r[3] = {WiFi.SSID(i), String(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
        if (r[0].length()) sendResult(C_SCAN, r, 3);
      }
      WiFi.scanDelete();
      sendResult(C_SCAN, nullptr, 0);
      break;
    }
    default: sendError(E_UNKNOWN_CMD);
  }
}

// Call often from loop(). Bytes that aren't Improv packets are ignored.
void loop() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    // plain-text commands share the port: collect lines and hand over any starting with "PW:"
    if (b == '\n' || b == '\r') { if (line.startsWith("PW:") && cmdFn) { line.trim(); cmdFn(line); } line = ""; }
    else if (line.length() < 64) line += (char)b; else line = "";
    if (rxLen < 6) {                                    // match the "IMPROV" header byte by byte
      if (b == "IMPROV"[rxLen]) rx[rxLen++] = b;
      else rxLen = (b == 'I') ? (rx[0] = b, 1) : 0;
      continue;
    }
    rx[rxLen++] = b;
    if (rxLen >= 9) {
      uint8_t len = rx[8];
      if (rxLen == (size_t)9 + len + 1) {
        uint8_t sum = 0; for (size_t i = 0; i < rxLen - 1; i++) sum += rx[i];
        if (sum == rx[rxLen - 1] && rx[6] == 1 && rx[7] == T_RPC) handle(rx + 9, len);
        rxLen = 0;
      }
    }
    if (rxLen >= sizeof(rx)) rxLen = 0;
  }
}
}  // namespace improv
