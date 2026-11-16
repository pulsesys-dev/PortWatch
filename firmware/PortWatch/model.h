// Data model + parsers for the AnyPS5 feed. No Arduino networking in here, so the
// same code is unit-tested on a PC against a full-size progress.json.
#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <algorithm>

struct Lib    { char name[32]; uint32_t done, todo; };
struct HistPt { int32_t day; uint32_t done, total; };
struct Area   { char name[28]; uint16_t count; };

struct Model {
  uint32_t done = 0, total = 0; float percent = 0;
  Lib *libs = nullptr; uint16_t nLibs = 0;
  bool hasShader = false, legacy = false;
  uint32_t fTotal = 0, fDecoded = 0, fTranslated = 0, dppTotal = 0, dppDecoded = 0, throws = 0;
  Area areas[8]; uint8_t nAreas = 0;
  uint16_t fmtTotal = 0, fmtSampled = 0, fmtBuffer = 0, fmtHost = 0;
  bool hasPr = false; uint32_t prDone = 0; float prPercent = 0;
  HistPt *hist = nullptr; uint16_t nHist = 0;
  time_t published = 0;           // Last-Modified of progress.json (UTC)
  ~Model() { free(libs); free(hist); }
  uint32_t libsWithFns() const { uint32_t n = 0; for (int i = 0; i < nLibs; i++) if (libs[i].done + libs[i].todo) n++; return n; }
  uint32_t libsComplete() const { uint32_t n = 0; for (int i = 0; i < nLibs; i++) if (libs[i].done + libs[i].todo && !libs[i].todo) n++; return n; }
};

// ---- helpers ----
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {   // Howard Hinnant
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}
static int32_t parseDay(const char *s) {   // "2026-11-12"
  if (!s || strlen(s) < 10) return 0;
  return daysFromCivil(atoi(s), atoi(s + 5), atoi(s + 8));
}
template <class Str> static time_t parseHttpDate(const Str &s) {   // "Thu, 13 Nov 2026 01:02:03 GMT"
  const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int d, y, hh, mm, ss; char mon[4] = {0};
  if (sscanf(s.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return 0;
  const char *p = strstr(mons, mon); if (!p) return 0;
  int m = (p - mons) / 3 + 1;
  return (time_t)daysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}
static void copyLabel(char *dst, size_t n, const char *src) {
  if (!src) src = "?";
  if (!strncmp(src, "libSce", 6) && src[6]) src += 6;
  strlcpy(dst, src, n);
}

static char errBuf[64];

// Parses progress.json from any ArduinoJson input (Stream, std::istream...). Returns nullptr or an error.
template <class TIn> const char *parseProgress(TIn &in, Model *m, ArduinoJson::Allocator *alloc = ArduinoJson::detail::DefaultAllocator::instance()) {
  JsonDocument filter;
  filter["libraries"]["done"] = true; filter["libraries"]["total"] = true; filter["libraries"]["percent"] = true;
  filter["libraries"]["groups"][0]["name"] = true;
  filter["libraries"]["groups"][0]["done"] = true;
  filter["libraries"]["groups"][0]["todo"] = true;
  filter["shaders"]["done"] = true; filter["shaders"]["total"] = true;
  JsonObject fs = filter["shader_source"].to<JsonObject>();
  fs["instructions"] = true; fs["dpp"] = true; fs["unsupported_throws"] = true; fs["formats"] = true;
  fs["throw_areas"][0]["name"] = true; fs["throw_areas"][0]["count"] = true;
  filter["open_pr_progress"]["available"] = true; filter["open_pr_progress"]["potential"] = true;

  JsonDocument doc(alloc);
  DeserializationError err = deserializeJson(doc, in, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(12));
  if (err) { snprintf(errBuf, sizeof errBuf, "Parse: %s", err.c_str()); return errBuf; }

  JsonObject L = doc["libraries"];
  if (L.isNull()) return "Feed has no libraries";
  m->done = L["done"] | 0; m->total = L["total"] | 0; m->percent = L["percent"] | 0.0f;
  JsonArray gs = L["groups"].as<JsonArray>();
  m->nLibs = 0;
  m->libs = (Lib *)malloc(sizeof(Lib) * std::max((size_t)1, gs.size()));
  if (!m->libs) return "Out of memory";
  for (JsonObject gr : gs) {
    Lib &l = m->libs[m->nLibs++];
    copyLabel(l.name, sizeof(l.name), gr["name"] | "?");
    l.done = gr["done"] | 0; l.todo = gr["todo"] | 0;
  }
  JsonObject ss = doc["shader_source"];
  if (!ss.isNull()) {
    m->hasShader = true;
    m->fTotal = ss["instructions"]["total"] | 0; m->fDecoded = ss["instructions"]["decoded"] | 0;
    m->fTranslated = ss["instructions"]["translated"] | 0;
    m->dppTotal = ss["dpp"]["total"] | 0; m->dppDecoded = ss["dpp"]["decoded"] | 0;
    m->throws = ss["unsupported_throws"] | 0;
    m->fmtTotal = ss["formats"]["total"] | 0; m->fmtSampled = ss["formats"]["sampled"] | 0;
    m->fmtBuffer = ss["formats"]["buffer"] | 0; m->fmtHost = ss["formats"]["host"] | 0;
    for (JsonObject a : ss["throw_areas"].as<JsonArray>()) {
      if (m->nAreas >= 8) break;
      strlcpy(m->areas[m->nAreas].name, a["name"] | "?", sizeof(m->areas[0].name));
      m->areas[m->nAreas++].count = a["count"] | 0;
    }
  } else if (!doc["shaders"].isNull()) {
    m->hasShader = true; m->legacy = true;
    m->fTotal = doc["shaders"]["total"] | 0; m->fDecoded = doc["shaders"]["done"] | 0;
  }
  if (doc["open_pr_progress"]["available"] | false) {
    m->hasPr = true;
    m->prDone = doc["open_pr_progress"]["potential"]["done"] | 0;
    m->prPercent = doc["open_pr_progress"]["potential"]["percent"] | 0.0f;
  }
  return nullptr;
}

// Parses history.json (array of daily points). Keeps the last 120 days.
template <class TIn> bool parseHistory(TIn &in, Model *m, ArduinoJson::Allocator *alloc = ArduinoJson::detail::DefaultAllocator::instance()) {
  JsonDocument filter;
  filter[0]["date"] = true;
  filter[0]["libraries"]["done"] = true;
  filter[0]["libraries"]["total"] = true;
  JsonDocument doc(alloc);
  DeserializationError err = deserializeJson(doc, in, DeserializationOption::Filter(filter));
  if (err) return false;
  JsonArray a = doc.as<JsonArray>();
  const size_t keep = 120;                       // last ~4 months is plenty for the chart
  size_t n = a.size(), skip = n > keep ? n - keep : 0, i = 0;
  m->hist = (HistPt *)malloc(sizeof(HistPt) * std::max((size_t)1, n - skip));
  if (!m->hist) return false;
  for (JsonObject p : a) {
    if (i++ < skip) continue;
    HistPt &h = m->hist[m->nHist++];
    h.day = parseDay(p["date"] | "");
    h.done = p["libraries"]["done"] | 0; h.total = p["libraries"]["total"] | 0;
  }
  return true;
}
