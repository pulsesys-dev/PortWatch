// Downloads and parses the AnyPS5 progress feed.
//
// progress.json is big (2–3 MB, mostly function-name lists). The board never holds it:
// ArduinoJson streams it straight off the TLS socket through a filter that keeps only
// the numbers, so peak RAM stays around 40 KB plus the TLS buffers.
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "model.h"

// ---- state shared with the UI ----
volatile uint32_t dlBytes = 0, dlTotal = 0;
volatile bool     fetching = false;
volatile Model   *pendingModel = nullptr;     // handed from the fetch task to loop()
String            fetchError;                  // last error, "" when fine
uint32_t          lastFetchOk = 0;             // millis() of last good check (200 or 304)
String            etag;
TaskHandle_t      fetchTaskHandle = nullptr;

// Buffered, byte-counting reader over the TLS socket (ArduinoJson reads one byte at a time).
class FeedStream : public Stream {
 public:
  explicit FeedStream(Client &c) : c_(c) {}
  int available() override { return (int)(len_ - pos_) + c_.available(); }
  int read() override { if (pos_ >= len_ && !fill()) return -1; return buf_[pos_++]; }
  int peek() override { if (pos_ >= len_ && !fill()) return -1; return buf_[pos_]; }
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char *out, size_t n) {
    size_t got = 0;
    while (got < n) {
      if (pos_ >= len_ && !fill()) break;
      size_t k = min(n - got, len_ - pos_);
      memcpy(out + got, buf_ + pos_, k); pos_ += k; got += k;
    }
    return got;
  }
 private:
  bool fill() {
    uint32_t t0 = millis();
    for (;;) {
      int a = c_.available();
      if (a > 0) {
        int r = c_.read(buf_, min((int)sizeof(buf_), a));
        if (r > 0) { len_ = r; pos_ = 0; dlBytes += r; return true; }
      }
      if (!c_.connected() && c_.available() <= 0) return false;
      if (millis() - t0 > 15000) return false;
      delay(2);
    }
  }
  Client &c_;
  uint8_t buf_[1536];
  size_t pos_ = 0, len_ = 0;
};

static bool openFeed(HTTPClient &http, WiFiClientSecure &tls, const String &url, bool conditional) {
  tls.setInsecure();   // public, read-only data; avoids shipping a CA bundle
  http.setReuse(false);
  http.useHTTP10(true);   // no chunked encoding, so the stream is the raw JSON
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(String(EP_NAME "/" EP_VERSION " (ESP32 desk display)"));
  if (!http.begin(tls, url)) return false;
  static const char *keys[] = {"ETag", "Last-Modified"};
  http.collectHeaders(keys, 2);
  if (conditional && etag.length()) http.addHeader("If-None-Match", etag);
  return true;
}

// Returns 1 = new data, 0 = unchanged (304), -1 = error (fetchError set)
static int fetchProgress(Model *m) {
  WiFiClientSecure tls; HTTPClient http;
  if (!openFeed(http, tls, feedBase() + "progress.json", true)) { fetchError = "Bad feed address"; return -1; }
  int code = http.GET();
  if (code == 304) { http.end(); return 0; }
  if (code != 200) { fetchError = code < 0 ? String("Network: ") + http.errorToString(code) : String("Feed HTTP ") + code; http.end(); return -1; }
  dlTotal = http.getSize() > 0 ? http.getSize() : 0; dlBytes = 0;
  String newTag = http.header("ETag");
  m->published = parseHttpDate(http.header("Last-Modified"));

  FeedStream stream(*http.getStreamPtr());
  const char *perr = parseProgress(stream, m);
  http.end();
  if (perr) { fetchError = perr; return -1; }
  etag = newTag;
  return 1;
}

static void fetchHistory(Model *m) {
  WiFiClientSecure tls; HTTPClient http;
  if (!openFeed(http, tls, feedBase() + "history.json", false)) return;
  if (http.GET() != 200) { http.end(); return; }
  FeedStream stream(*http.getStreamPtr());
  parseHistory(stream, m);
  http.end();
}

void fetchTask(void *) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !pendingModel) {
      fetching = true; dlBytes = 0; dlTotal = 0;
      Model *m = new Model();
      int r = fetchProgress(m);
      if (r == 1) {
        fetchHistory(m);
        fetchError = ""; lastFetchOk = millis();
        pendingModel = m;
      } else {
        if (r == 0) { fetchError = ""; lastFetchOk = millis(); }
        delete m;
      }
      fetching = false;
      Serial.printf("Feed check: %s, heap %lu\n", r == 1 ? "new data" : r == 0 ? "unchanged" : fetchError.c_str(), (unsigned long)ESP.getFreeHeap());
    }
    uint32_t waitMs = (fetchError.length() ? 60000UL : (uint32_t)S.refreshMin * 60000UL);
    if (WiFi.status() != WL_CONNECTED) waitMs = 5000;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));   // woken early by "refresh now"
  }
}

void fetchBegin() {
  xTaskCreatePinnedToCore(fetchTask, "feed", 16384, nullptr, 1, &fetchTaskHandle, 0);
}
void fetchNow() { if (fetchTaskHandle) xTaskNotifyGive(fetchTaskHandle); }

// ---- "moved since the last update" ----
struct Mover { char name[32]; int32_t delta; uint32_t done, total; };
Mover movers[16]; uint8_t nMovers = 0;
struct SnapEntry { uint32_t hash; uint32_t done; };

static uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

void moversLoad() {
  prefs.begin("portwatch", true);
  size_t n = prefs.getBytes("movers", movers, sizeof(movers));
  nMovers = prefs.getUChar("nMovers", 0);
  prefs.end();
  if (n != sizeof(movers)) nMovers = 0;
}

// Compare the new model with the last one this board saw (kept in flash across power cycles).
// Returns the gain in implemented functions (0 when nothing changed or first run).
int32_t applySnapshot(const Model *m) {
  prefs.begin("portwatch", false);
  uint32_t oldDone = prefs.getUInt("snapDone", 0), oldTotal = prefs.getUInt("snapTotal", 0);
  bool had = prefs.isKey("snap");
  bool changed = !had || oldDone != m->done || oldTotal != m->total;
  int32_t gain = 0;
  if (changed) {
    if (had) {
      size_t sz = prefs.getBytesLength("snap");
      SnapEntry *old = (SnapEntry *)malloc(max((size_t)8, sz));
      size_t no = old ? prefs.getBytes("snap", old, sz) / sizeof(SnapEntry) : 0;
      nMovers = 0;
      Mover tmp[16]; uint8_t nt = 0;
      for (int i = 0; i < m->nLibs; i++) {
        uint32_t h = fnv(m->libs[i].name); int32_t prev = -1;
        for (size_t j = 0; j < no; j++) if (old[j].hash == h) { prev = old[j].done; break; }
        if (prev < 0) continue;
        int32_t d = (int32_t)m->libs[i].done - prev;
        if (!d) continue;
        // keep the 16 biggest gains
        int slot = nt < 16 ? nt++ : -1;
        if (slot < 0) { int mi = 0; for (int k = 1; k < 16; k++) if (tmp[k].delta < tmp[mi].delta) mi = k; if (tmp[mi].delta >= d) continue; slot = mi; }
        strlcpy(tmp[slot].name, m->libs[i].name, sizeof(tmp[0].name));
        tmp[slot].delta = d; tmp[slot].done = m->libs[i].done; tmp[slot].total = m->libs[i].done + m->libs[i].todo;
      }
      free(old);
      for (int a = 0; a < nt; a++) for (int b = a + 1; b < nt; b++) if (tmp[b].delta > tmp[a].delta) { Mover t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t; }
      memcpy(movers, tmp, sizeof(Mover) * nt); nMovers = nt;
      prefs.putBytes("movers", movers, sizeof(movers)); prefs.putUChar("nMovers", nMovers);
      gain = (int32_t)m->done - (int32_t)oldDone;
    }
    SnapEntry *snap = (SnapEntry *)malloc(sizeof(SnapEntry) * max((uint16_t)1, m->nLibs));
    if (snap) {
      for (int i = 0; i < m->nLibs; i++) { snap[i].hash = fnv(m->libs[i].name); snap[i].done = m->libs[i].done; }
      prefs.putBytes("snap", snap, sizeof(SnapEntry) * m->nLibs);
      free(snap);
    }
    prefs.putUInt("snapDone", m->done); prefs.putUInt("snapTotal", m->total);
  }
  prefs.end();
  return gain;
}
