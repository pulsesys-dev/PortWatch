// Screens. Everything is laid out from W/H so the same code runs on 320x240 (CYD) and 480x272 (Guition).
#pragma once
#include <WiFi.h>
#include "board.h"
#include "feed.h"
#include "FreeSans9pt7b.h"
#include "FreeSansBold9pt7b.h"
#include "FreeSansBold12pt7b.h"
#include "FreeSansBold18pt7b.h"
#include "FreeSansBold24pt7b.h"

constexpr uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F); }
const uint16_t C_BG = rgb(0x05070d), C_PANEL = rgb(0x0b1120), C_LINE = rgb(0x1b2745), C_TRACK = rgb(0x16213d),
               C_FG = rgb(0xe8eefc), C_MUTED = rgb(0x8190b4), C_DIM = rgb(0x4c5a7d),
               C_ACCENT = rgb(0x2f7bff), C_ACCENT2 = rgb(0x7fb0ff), C_AREA = rgb(0x0f2350),
               C_OK = rgb(0x36d399), C_WARN = rgb(0xf5b83d), C_BAD = rgb(0xff5d6c), C_CELEB = rgb(0x062a1c);

const GFXfont *F_S = &FreeSans9pt7b, *F_SB = &FreeSansBold9pt7b, *F_M = &FreeSansBold12pt7b, *F_L = &FreeSansBold18pt7b;
#if EP_GUITION
const GFXfont *F_XL = &FreeSansBold24pt7b;
const int HDR = 30, ROWH = 34;
#else
const GFXfont *F_XL = &FreeSansBold18pt7b;
const int HDR = 26, ROWH = 30;
#endif
#define F_TINY ((const GFXfont *)nullptr)

enum Screen : uint8_t { SC_OVERVIEW, SC_HISTORY, SC_DAILY, SC_LIBS, SC_INFO, SC_COUNT };
const char *screenNames[SC_COUNT] = {"Overview", "Last 7 days", "Daily progress", "Libraries", "Device info"};
// Device info is always available (double-tap to it, or hold the screen); auto-rotate skips it
inline bool screenEnabled(uint8_t s) { return s == SC_INFO || (S.screenMask >> s) & 1; }
uint8_t screen = SC_OVERVIEW, libMode = 1;   // 0 moved, 1 almost done, 2 most left, 3 biggest
Model *model = nullptr;

// ---------- text helpers ----------
void txt(const GFXfont *f, int x, int y, const char *s, uint16_t c, uint8_t align = 0) {
  g->setFont(f); g->setTextSize(1); g->setTextColor(c);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  if (align == 1) x -= (int)(w / 2) + x1; else if (align == 2) x -= (int)w + x1;
  g->setCursor(x, f ? y : y - 7);
  g->print(s);
}
int textW(const GFXfont *f, const char *s) {
  g->setFont(f); g->setTextSize(1);
  int16_t x1, y1; uint16_t w, h; g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w + x1;
}
void fit(const GFXfont *f, char *s, int maxW) {     // shorten with ".." until it fits
  if (textW(f, s) <= maxW) return;
  size_t n = strlen(s);
  while (n > 1) { s[--n] = 0; char t[48]; snprintf(t, sizeof t, "%s..", s); if (textW(f, t) <= maxW) { strcpy(s, t); return; } }
}
void fmtNum(uint32_t v, char *out) {
  char t[16]; snprintf(t, sizeof t, "%lu", (unsigned long)v);
  int n = strlen(t), o = 0;
  for (int i = 0; i < n; i++) { out[o++] = t[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
  out[o] = 0;
}
void fmtSigned(int32_t v, char *out) { char t[16]; fmtNum(abs(v), t); sprintf(out, "%s%s", v < 0 ? "-" : "+", t); }
void fmtShort(uint32_t v, char *out) { if (v >= 1000) sprintf(out, v % 1000 ? "%.1fk" : "%.0fk", v / 1000.0); else sprintf(out, "%lu", (unsigned long)v); }

void bar(int x, int y, int w, int h, float p, uint16_t col) {
  g->fillRoundRect(x, y, w, h, h / 2, C_TRACK);
  int fw = constrain((int)(w * p + 0.5f), 0, w);
  if (fw > 0) g->fillRoundRect(x, y, max(fw, (int)h), h, h / 2, p >= 0.999f ? C_OK : col);
}
void ringArc(int cx, int cy, int r, int th, float p, uint16_t col) {
  if (p <= 0.002f) return;
  if (p >= 0.999f) { g->fillArc(cx, cy, r, r - th, 0, 360, col); return; }
  float end = 270 + 360 * p;
  if (end <= 360) g->fillArc(cx, cy, r, r - th, 270, end, col);
  else { g->fillArc(cx, cy, r, r - th, 270, 360, col); g->fillArc(cx, cy, r, r - th, 0, end - 360, col); }
}
void dashLine(int x0, int y0, int x1, int y1, uint16_t c) {
  float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  int steps = max(1, (int)(len / 4));
  for (int i = 0; i < steps; i += 2) {
    float a = (float)i / steps, b = min(1.0f, (float)(i + 1) / steps);
    g->drawLine(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b, c);
  }
}

// ---------- status ----------
String agoText(uint32_t secs) {
  if (secs < 90) return "just now";
  if (secs < 5400) return String(secs / 60) + "m ago";
  if (secs < 129600) return String(secs / 3600) + "h ago";
  return String(secs / 86400) + "d ago";
}
bool timeValid() { return time(nullptr) > 1700000000; }

void header(const char *title, const char *right = nullptr, uint16_t rightCol = 0) {
  g->fillRect(0, 0, W, HDR, C_BG);
  // tiny pulse mark
  int y = HDR / 2;
  g->drawLine(8, y, 12, y, C_ACCENT2); g->drawLine(12, y, 15, y - 6, C_ACCENT2); g->drawLine(15, y - 6, 19, y + 6, C_ACCENT2);
  g->drawLine(19, y + 6, 22, y - 2, C_ACCENT2); g->drawLine(22, y - 2, 25, y, C_ACCENT2);
  // status at right (drawn after the title, which is shortened to fit beside it)
  String r;
  uint16_t dot = C_OK;
  if (WiFi.status() != WL_CONNECTED) { r = "no Wi-Fi"; dot = C_BAD; }
  else if (fetching) { r = dlTotal ? String("updating ") + (int)(100.0 * dlBytes / dlTotal) + "%" : String("updating ") + (dlBytes / 1024) + "KB"; dot = C_WARN; }
  else if (fetchError.length()) { r = "feed error"; dot = C_BAD; }
  else if (model && model->published && timeValid()) r = agoText(time(nullptr) - model->published);
  else if (lastFetchOk) r = "checked " + agoText((millis() - lastFetchOk) / 1000);
  if (right && W >= 300) { r = right; dot = rightCol ? rightCol : dot; }
  if (W < 300 && r.length() > 16) r = r.substring(0, 15) + ".";
  int rw = textW(F_TINY, r.c_str());
  txt(F_TINY, W - 8, y + 4, r.c_str(), C_DIM, 2);
  g->fillCircle(W - 14 - rw, y, 3, dot);
  if (W < 300) {   // shorter titles on the narrow portrait CYD
    static const char *longT[] = {"SYSTEM LIBRARIES", "SHADER RECOMPILER", "EVERY LIBRARY"}, *shortT[] = {"OVERVIEW", "SHADERS", "ALL LIBRARIES"};
    for (int i = 0; i < 3; i++) if (!strcmp(title, longT[i])) title = shortT[i];
  }
  char tt[40]; strlcpy(tt, title, sizeof tt); fit(F_SB, tt, W - 32 - rw - 26);
  txt(F_SB, 32, y + 6, tt, C_MUTED);
}

void pageDots() {
  int n = 0; for (int i = 0; i < SC_COUNT; i++) if (screenEnabled(i)) n++;
  int x = W / 2 - (n - 1) * 5, k = 0;
  for (int i = 0; i < SC_COUNT; i++) if (screenEnabled(i)) {
    g->fillCircle(x + k * 10, H - 6, 2, i == screen ? C_ACCENT2 : C_LINE); k++;
  }
}

// ---------- gains from history ----------
struct Gains { bool ok; int32_t day, week, month; };
Gains gains() {
  Gains r{false, 0, 0, 0};
  if (!model || model->nHist < 2) return r;
  const HistPt *h = model->hist; int n = model->nHist;
  const HistPt &last = h[n - 1];
  auto back = [&](int days) { int i = n - 1; while (i > 0 && h[i].day > last.day - days) i--; return h[i]; };
  r.ok = true;
  r.day = (int32_t)last.done - (int32_t)h[n - 2].done;
  r.week = (int32_t)last.done - (int32_t)back(7).done;
  r.month = (int32_t)last.done - (int32_t)back(30).done;
  return r;
}

// ---------- screens ----------
void statRow(int x, int w, int y, const char *label, const char *value, uint16_t col) {
  txt(F_S, x, y, label, C_MUTED);
  char v[48]; strlcpy(v, value, sizeof v); fit(F_SB, v, w - textW(F_S, label) - 10);
  txt(F_SB, x + w, y, v, col, 2);
}

void drawOverview() {
  header("SYSTEM LIBRARIES");
  int top = HDR + 2, bottom = H - 14, avail = bottom - top;
  bool tall = H > W;
  // landscape: ring on the left, numbers on the right. portrait: ring on top, numbers underneath
  int R = tall ? min((int)(W * 0.36f), (int)(avail * 0.22f)) : min(avail / 2 - 6, (int)(W * 0.2f));
  int th = max(9, R / 6);
  int cx = tall ? W / 2 : 12 + R, cy = tall ? top + 8 + R : top + avail / 2;
  float p = model->total ? (float)model->done / model->total : 0;
  g->fillArc(cx, cy, R, R - th, 0, 360, C_TRACK);
  ringArc(cx, cy, R, th, p, C_ACCENT);
  char b[24]; snprintf(b, sizeof b, "%.2f%%", model->percent > 0 ? model->percent : p * 100);
  if (R >= 80) { txt(F_L, cx, cy + 6, b, C_FG, 1); txt(F_S, cx, cy + 28, "implemented", C_MUTED, 1); }
  else         { txt(F_M, cx, cy + 5, b, C_FG, 1); txt(F_TINY, cx, cy + 22, "implemented", C_MUTED, 1); }
  int x0, cw, y;
  char t[24], of[40];
  fmtNum(model->total, t); snprintf(of, sizeof of, "of %s functions", t);
  if (tall) {
    x0 = 14; cw = W - 28;
    y = cy + R + (H > 400 ? 44 : 34);
    fmtNum(model->done, b); txt(F_XL, W / 2, y, b, C_FG, 1);
    y += H > 400 ? 24 : 18; fit(F_S, of, cw); txt(F_S, W / 2, y, of, C_MUTED, 1);
  } else {
    x0 = cx + R + 16; cw = W - x0 - 10;
    y = top + (H > 250 ? 40 : 30);
    fmtNum(model->done, b); txt(F_XL, x0, y, b, C_FG);
    y += H > 250 ? 24 : 20; fit(F_S, of, cw); txt(F_S, x0, y, of, C_MUTED);
  }
  int step = (H > 250 && !(tall && H < 400)) ? 25 : 21; y += step + (tall ? 2 : 6);
  bool narrow = cw < 240;
  Gains gn = gains();
  if (gn.ok) {
    fmtSigned(gn.week, b);  statRow(x0, cw, y, "7 days", b, gn.week > 0 ? C_OK : C_FG); y += step;
    if (y < bottom) { fmtSigned(gn.month, b); statRow(x0, cw, y, "30 days", b, gn.month > 0 ? C_OK : C_FG); y += step; }
  }
  if (y < bottom) {
    snprintf(b, sizeof b, "%lu/%lu", (unsigned long)model->libsComplete(), (unsigned long)model->libsWithFns());
    statRow(x0, cw, y, "Libs done", b, C_FG); y += step;
  }
  if (gn.ok && y < bottom) { fmtSigned(gn.day, b); statRow(x0, cw, y, "Last day", b, gn.day > 0 ? C_OK : C_FG); y += step; }
  if (model->hasPr && y < bottom) { fmtNum(model->prDone, t); statRow(x0, cw, y, narrow ? "Open PRs" : "With open PRs", t, C_ACCENT2); }
  pageDots();
}

// Candle chart: % implemented on the vertical axis, calendar days across.
// Each day with a commit gets a candle that opens at the previous point's % and closes at that
// day's %. Green = % went up, red = % went down (usually because more functions were declared).
// Blue bars along the bottom = functions implemented that day.
void drawHistory() {
  Gains gn = gains();
  char r[32] = "";
  if (gn.ok) { char t[16]; fmtSigned(gn.week, t); snprintf(r, sizeof r, "%s functions", t); }
  header("LAST 7 DAYS  %", gn.ok ? r : nullptr, C_OK);
  if (!model->hist || model->nHist < 2) { txt(F_S, W / 2, H / 2, "No history yet", C_DIM, 1); pageDots(); return; }
  const HistPt *h = model->hist; int n = model->nHist;
  const int padL = 32, padR = 6, top = HDR + 8, volH = H * 0.12f, bottom = H - 14 - volH - 16;
  const int cw = W - padL - padR, ch = bottom - top;
  int32_t dLast = h[n - 1].day;
  const int days = 7;
  int32_t dFirst = dLast - days + 1;
  int first = 0; while (first < n - 1 && h[first].day < dFirst) first++;
  auto pct = [&](int i) { return h[i].total ? 100.0f * h[i].done / h[i].total : 0.0f; };
  float lo = 100, hi = 0; int32_t maxG = 1;
  if (h[first].day < dFirst) first = n;   // nothing inside the last 7 days
  if (first >= n) { lo = 0; hi = 100; }
  for (int i = first; i < n; i++) {
    float o = i ? pct(i - 1) : pct(i), c = pct(i);
    lo = min(lo, min(o, c)); hi = max(hi, max(o, c));
    if (i) maxG = max(maxG, (int32_t)h[i].done - (int32_t)h[i - 1].done);
  }
  float span = max(2.0f, hi - lo); lo = max(0.0f, lo - span * 0.08f); hi = min(100.0f, hi + span * 0.08f);
  float raw = (hi - lo) / 4, mag = powf(10, floorf(log10f(raw))), nn = raw / mag;
  float step = (nn < 1.5f ? 1 : nn < 3 ? 2 : nn < 7 ? 5 : 10) * mag;
  lo = floorf(lo / step) * step; hi = ceilf(hi / step) * step; if (hi <= lo) hi = lo + step;
  float slot = (float)cw / days;
  auto X = [&](int32_t d) { return padL + (int)((d - dFirst + 0.5f) * slot); };
  auto Y = [&](float v) { return bottom - (int)((v - lo) / (hi - lo) * ch); };
  for (float v = lo; v <= hi + 0.001f; v += step) {
    g->drawFastHLine(padL, Y(v), cw, C_TRACK);
    char t[12]; snprintf(t, sizeof t, step < 1 ? "%.1f%%" : "%.0f%%", v); txt(F_TINY, padL - 3, Y(v) + 4, t, C_DIM, 2);
  }
  // day labels, ~44 px apart, counted back from the latest day
  const int steps[] = {1, 2, 3, 7, 14, 30}; int every = 30;
  for (int sDays : steps) if (sDays * slot >= 34) { every = sDays; break; }
  for (int32_t d = dLast; d >= dFirst; d -= every) {
    time_t t = (time_t)d * 86400; struct tm tmv; gmtime_r(&t, &tmv);
    char lb[8]; snprintf(lb, sizeof lb, "%d/%d", tmv.tm_mday, tmv.tm_mon + 1);
    g->drawFastVLine(X(d), bottom + 1, 3, C_LINE);
    int lx = min(X(d), W - padR - textW(F_TINY, lb) / 2);
    txt(F_TINY, lx, bottom + 13, lb, C_DIM, 1);
  }
  int bw = max(1, min(12, (int)(slot * 0.66f)));
  int vBase = H - 12;
  for (int i = first; i < n; i++) {
    float o = i ? pct(i - 1) : pct(i), c = pct(i);
    bool up = c >= o; uint16_t col = up ? C_OK : C_BAD;
    int x = X(h[i].day), yt = Y(max(o, c)), yb = Y(min(o, c));
    int bh = max(2, yb - yt);
    g->drawFastVLine(x, yt - 2, bh + 4, col);                  // wick
    g->fillRect(x - bw / 2, yt, max(1, bw), bh, col);          // body
    int32_t gain = i ? (int32_t)h[i].done - (int32_t)h[i - 1].done : 0;
    if (gain > 0) { int vh = max(1, (int)((int64_t)gain * volH / maxG)); g->fillRect(x - bw / 2, vBase - vh, max(1, bw), vh, C_ACCENT); }
  }
  pageDots();
}

// Daily progress: the latest commit day big, then one row per calendar day (newest first):
// functions implemented that day (bar + number) and how the % moved.
static const char *WD[7] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};   // day 0 = 1 Jan 1970, a Thursday
static void dayLabel(int32_t day, char *out, size_t n) {
  time_t t = (time_t)day * 86400; struct tm tmv; gmtime_r(&t, &tmv);
  snprintf(out, n, "%s %d", WD[((day % 7) + 7) % 7], tmv.tm_mday);
}
void drawDaily() {
  if (!model->hist || model->nHist < 2) { header("DAILY PROGRESS"); txt(F_S, W / 2, H / 2, "No history yet", C_DIM, 1); pageDots(); return; }
  const HistPt *h = model->hist; int n = model->nHist;
  const HistPt &last = h[n - 1], &prev = h[n - 2];
  int32_t gain = (int32_t)last.done - (int32_t)prev.done;
  float pl = last.total ? 100.0f * last.done / last.total : 0, pp = prev.total ? 100.0f * prev.done / prev.total : 0;
  char d[16]; dayLabel(last.day, d, sizeof d);
  char r[24]; snprintf(r, sizeof r, "latest %s", d);
  header("DAILY PROGRESS", r, C_ACCENT2);
  // summary: latest day's gain, big
  char b[24]; fmtSigned(gain, b);
  int y = HDR + (H > 250 ? 40 : 34);
  txt(F_XL, 12, y, b, gain > 0 ? C_OK : C_DIM);
  int bx = 12 + textW(F_XL, b) + 12;
  txt(F_S, bx, y - (H > 250 ? 20 : 17), "functions", C_MUTED);
  snprintf(b, sizeof b, "%+.2f pts", pl - pp);
  txt(F_SB, bx, y + 1, b, pl >= pp ? C_OK : C_BAD);
  // one row per calendar day
  int rowH = H > 250 ? 26 : 22, top = y + 14, rows = min(14, (H - 14 - top) / rowH);
  auto at = [&](int32_t day) -> int { for (int i = n - 1; i >= 0 && h[i].day >= day; i--) if (h[i].day == day) return i; return -1; };
  int32_t maxG = 1;
  for (int k = 0; k < rows; k++) { int i = at(last.day - k); if (i > 0) maxG = max(maxG, (int32_t)h[i].done - (int32_t)h[i - 1].done); }
  int dateW = textW(F_S, "Wed 30") + 8, ptsW = W >= 300 ? textW(F_TINY, "+0.00") + 10 : 0, numW = textW(F_SB, "+8,888") + 8;
  int barX = 10 + dateW, barW = W - 10 - barX - numW - ptsW;
  for (int k = 0; k < rows; k++) {
    int32_t day = last.day - k; int i = at(day);
    int ry = top + k * rowH + rowH - 7;
    if (k % 2 == 0) g->fillRect(6, ry - rowH + 9, W - 12, rowH - 2, C_PANEL);
    char lbl[16]; dayLabel(day, lbl, sizeof lbl);
    txt(F_S, 10, ry, lbl, i >= 0 ? C_FG : C_DIM);
    if (i <= 0) { txt(F_TINY, barX, ry - 1, i == 0 ? "first day" : "no commits", C_DIM); continue; }
    int32_t gday = (int32_t)h[i].done - (int32_t)h[i - 1].done;
    if (gday > 0) g->fillRoundRect(barX, ry - 9, max(3, (int)((int64_t)gday * barW / maxG)), 7, 3, C_ACCENT);
    fmtSigned(gday, b); txt(F_SB, W - 10 - ptsW, ry, b, gday > 0 ? C_FG : C_DIM, 2);
    if (ptsW) {
      float a1 = h[i].total ? 100.0f * h[i].done / h[i].total : 0, a0 = h[i - 1].total ? 100.0f * h[i - 1].done / h[i - 1].total : 0;
      snprintf(b, sizeof b, "%+.2f", a1 - a0); txt(F_TINY, W - 10, ry - 1, b, a1 >= a0 ? C_OK : C_BAD, 2);
    }
  }
  pageDots();
}

const char *libModeName[4] = {"MOVED", "ALMOST DONE", "MOST LEFT", "BIGGEST"};
void drawLibs() {
  header(libModeName[libMode], "tap centre: sort", C_ACCENT2);
  int rows = (H - HDR - 14) / ROWH, y = HDR + 2;
  struct Row { const char *name; uint32_t done, total; int32_t delta; } list[16];
  if (rows > 16) rows = 16;
  int n = 0;
  if (libMode == 0) {
    for (int i = 0; i < nMovers && n < rows; i++) list[n++] = {movers[i].name, movers[i].done, movers[i].total, movers[i].delta};
  } else {
    // pick the top `rows` libraries for this sort without sorting everything
    bool used[512] = {false};
    for (int k = 0; k < rows; k++) {
      int best = -1; float bestScore = -1;
      for (int i = 0; i < model->nLibs && i < 512; i++) {
        const Lib &l = model->libs[i]; uint32_t t = l.done + l.todo;
        if (!t || used[i]) continue;
        float s;
        if (libMode == 1) { if (!l.todo) continue; s = (float)l.done / t * 100000 - l.todo; }
        else if (libMode == 2) s = l.todo;
        else s = t;
        if (s > bestScore) { bestScore = s; best = i; }
      }
      if (best < 0) break;
      used[best] = true;
      const Lib &l = model->libs[best];
      list[n++] = {l.name, l.done, l.done + l.todo, 0};
    }
  }
  if (!n) {
    txt(F_S, W / 2, H / 2 - 8, "No changes recorded yet", C_MUTED, 1);
    txt(F_TINY, W / 2, H / 2 + 14, "Libraries that move show here after the next feed update", C_DIM, 1);
  }
  for (int i = 0; i < n; i++) {
    char right[40], a[16], b[16];
    fmtNum(list[i].done, a); fmtNum(list[i].total, b);
    snprintf(right, sizeof right, "%s/%s", a, b);
    int rw = textW(F_S, right);
    char nm[48]; strlcpy(nm, list[i].name, sizeof nm);
    int dw = 0; char dl[16] = "";
    if (list[i].delta) { fmtSigned(list[i].delta, dl); dw = textW(F_SB, dl) + 8; }
    fit(F_S, nm, W - 20 - rw - 12 - dw);
    int base = y + ROWH - 13;
    txt(F_S, 10, base, nm, C_FG);
    if (dw) txt(F_SB, 10 + textW(F_S, nm) + 8, base, dl, list[i].delta > 0 ? C_OK : C_BAD);
    txt(F_S, W - 10, base, right, C_MUTED, 2);
    bar(10, base + 5, W - 20, 4, list[i].total ? (float)list[i].done / list[i].total : 0, C_ACCENT);
    y += ROWH;
  }
  pageDots();
}

// ---------- full-screen messages ----------
void messageScreen(const char *title, const char *l1, const char *l2 = nullptr, const char *l3 = nullptr, float progress = -1) {
  g->fillScreen(C_BG);
  header(title, "", C_DIM);
  int y = H / 2 - 30;
  char b[64];
  strlcpy(b, l1, sizeof b); const GFXfont *f1 = textW(F_M, b) > W - 16 ? F_SB : F_M; fit(f1, b, W - 16); txt(f1, W / 2, y, b, C_FG, 1);
  if (l2) { strlcpy(b, l2, sizeof b); fit(F_S, b, W - 16); txt(F_S, W / 2, y + 30, b, C_MUTED, 1); }
  if (l3) { strlcpy(b, l3, sizeof b); fit(F_S, b, W - 16); txt(F_S, W / 2, y + 54, b, C_ACCENT2, 1); }
  if (progress >= 0) bar(W / 6, y + 74, W * 2 / 3, 8, progress, C_ACCENT);
  flush();
}

void drawCelebration(int32_t gain) {
  g->fillScreen(C_CELEB);
  char b[24]; fmtSigned(gain, b);
  txt(F_XL, W / 2, H / 2 + 6, b, C_OK, 1);
  txt(F_SB, W / 2, H / 2 + 36, "functions implemented", C_FG, 1);
  txt(F_S, W / 2, H / 2 + 58, W < 300 ? "since the last update" : "since the last AnyPS5 update", C_MUTED, 1);
  txt(F_SB, W / 2, HDR + 10, "AnyPS5 moved forward", C_OK, 1);
  flush();
}

void drawInfo() {
  header("DEVICE INFO", EP_VERSION, C_ACCENT2);
  int y = HDR + 22, step = H > 250 ? 24 : 21, x = 12, w = W - 24;
  char b[64];
  statRow(x, w, y, "Address", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected", C_FG); y += step;
  statRow(x, w, y, W < 300 ? "Settings" : "Settings page", "portwatch.local", C_ACCENT2); y += step;
  String ss = WiFi.SSID(); strlcpy(b, ss.c_str(), sizeof b); fit(F_SB, b, w - 80);
  statRow(x, w, y, "Wi-Fi", b, C_FG); y += step;
  snprintf(b, sizeof b, "%d dBm", WiFi.RSSI()); statRow(x, w, y, "Signal", b, C_FG); y += step;
  statRow(x, w, y, "Board", EP_BOARD, C_FG); y += step;
  statRow(x, w, y, "Touch", touchName(), C_FG); y += step;
  snprintf(b, sizeof b, "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024)); statRow(x, w, y, "Memory", b, C_FG); y += step;
  if (model && model->published && timeValid() && y < H - 14) {
    struct tm tmv; localtime_r(&model->published, &tmv);
    strftime(b, sizeof b, "%d %b %H:%M", &tmv); statRow(x, w, y, "Feed published", b, C_FG); y += step;
  }
  if (fetchError.length() && y < H - 14) { strlcpy(b, fetchError.c_str(), sizeof b); statRow(x, w, y, "Last error", b, C_BAD); }
  pageDots();
}

void drawScreen() {
  if (!model && screen != SC_INFO) return;
  g->fillScreen(C_BG);
  switch (screen) {
    case SC_OVERVIEW: drawOverview(); break;
    case SC_HISTORY:  drawHistory(); break;
    case SC_DAILY:    drawDaily(); break;
    case SC_LIBS:     drawLibs(); break;
    case SC_INFO:     drawInfo(); break;
  }
  flush();
}

// header-only refresh (status text) without redrawing the whole screen
void drawHeaderOnly() {
  if (!model) return;
  if (screen != SC_OVERVIEW) return;   // other screens show fixed text on the right
  header("SYSTEM LIBRARIES");
  if (canvas) canvas->flush();
}

// ---------- touch calibration (resistive panels) ----------
// Four targets: three corners to work out the mapping, then the centre to check it.
// The mapping is stored in landscape coordinates, so it keeps working if you change orientation.
void crosshair(int x, int y, uint16_t c) { g->drawFastHLine(x - 14, y, 29, c); g->drawFastVLine(x, y - 14, 29, c); g->drawCircle(x, y, 7, c); g->fillCircle(x, y, 2, c); }
bool waitRaw(int16_t &rx, int16_t &ry) {
  // wait for a press, average the steady part of it, wait for release
  for (;;) {
    uint32_t t0 = millis(); int16_t a, b;
    while (!touchRaw(a, b)) { if (millis() - t0 > 60000) return false; delay(10); }
    long sx = 0, sy = 0; int n = 0, skip = 3;
    while (touchRaw(a, b)) { if (skip) skip--; else if (n < 64) { sx += a; sy += b; n++; } delay(8); }
    delay(200);
    if (n >= 3) { rx = sx / n; ry = sy / n; return true; }
  }
}
// solve lx = a*rx + b*ry + c (and the same for ly) through three points
static bool solve3(const float rx[3], const float ry[3], const float v[3], float &a, float &b, float &c) {
  float det = rx[0] * (ry[1] - ry[2]) - ry[0] * (rx[1] - rx[2]) + (rx[1] * ry[2] - rx[2] * ry[1]);
  if (fabsf(det) < 1) return false;
  a = (v[0] * (ry[1] - ry[2]) - ry[0] * (v[1] - v[2]) + (v[1] * ry[2] - v[2] * ry[1])) / det;
  b = (rx[0] * (v[1] - v[2]) - v[0] * (rx[1] - rx[2]) + (rx[1] * v[2] - rx[2] * v[1])) / det;
  c = (rx[0] * (ry[1] * v[2] - ry[2] * v[1]) - ry[0] * (rx[1] * v[2] - rx[2] * v[1]) + v[0] * (rx[1] * ry[2] - rx[2] * ry[1])) / det;
  return true;
}
void calTarget(int i, int n, int x, int y) {
  g->fillScreen(C_BG);
  char t[24]; snprintf(t, sizeof t, "Touch calibration  %d/%d", i + 1, n);
  txt(F_SB, W / 2, H / 2 - 10, t, C_FG, 1);
  txt(F_TINY, W / 2, H / 2 + 12, W < 300 ? "tap the centre of the cross" : "tap the centre of the cross with a stylus", C_MUTED, 1);
  crosshair(x, y, C_ACCENT2);
  flush();
}
// Returns true when a calibration was saved.
bool runCalibration() {
  if (!touchIsResistive()) return false;
  const int m = 22;
  for (;;) {
    int sx[4] = {m, W - 1 - m, m, W / 2}, sy[4] = {m, m, H - 1 - m, H / 2};
    float rx[4], ry[4];
    for (int i = 0; i < 4; i++) {
      calTarget(i, 4, sx[i], sy[i]);
      int16_t a, b; if (!waitRaw(a, b)) return false;
      rx[i] = a; ry[i] = b;
    }
    float lx[3], ly[3];
    for (int i = 0; i < 3; i++) { int16_t x, y; screenToLand(sx[i], sy[i], x, y); lx[i] = x; ly[i] = y; }
    Cal c{}; bool ok = solve3(rx, ry, lx, c.a, c.b, c.c) && solve3(rx, ry, ly, c.d, c.e, c.f);
    float err = 999;
    if (ok) {   // check against the centre tap
      int16_t ex, ey; screenToLand(sx[3], sy[3], ex, ey);
      float px = c.a * rx[3] + c.b * ry[3] + c.c, py = c.d * rx[3] + c.e * ry[3] + c.f;
      err = sqrtf((px - ex) * (px - ex) + (py - ey) * (py - ey));
    }
    Serial.printf("PW:CAL error %.1f px\n", err);
    if (!ok || err > 18) {
      messageScreen("CALIBRATION", "Let's try that again", "taps didn't line up", "tap anywhere to restart");
      int16_t a, b; waitRaw(a, b);
      continue;
    }
    c.magic = CAL_MAGIC; cal = c; calSave();
    // check screen: dots should land under your finger. Redo or Done.
    g->fillScreen(C_BG);
    txt(F_SB, W / 2, HDR + 16, "Touch calibrated", C_OK, 1);
    txt(F_TINY, W / 2, HDR + 34, "draw on the screen to check", C_MUTED, 1);
    int bw = min(110, W / 2 - 18), by = H - 46;
    g->fillRoundRect(W / 2 - bw - 6, by, bw, 34, 10, C_LINE); txt(F_SB, W / 2 - 6 - bw / 2, by + 23, "Redo", C_FG, 1);
    g->fillRoundRect(W / 2 + 6, by, bw, 34, 10, C_ACCENT); txt(F_SB, W / 2 + 6 + bw / 2, by + 23, "Done", C_FG, 1);
    flush();
    uint32_t t0 = millis();
    for (;;) {
      int16_t x, y;
      if (touchRead(x, y)) {
        t0 = millis();
        if (y >= by - 4) {
          while (touchRead(x, y)) delay(10);
          if (x < W / 2) break;          // Redo
          return true;                   // Done
        }
        g->fillCircle(x, y, 2, C_ACCENT2); flush();
      }
      if (millis() - t0 > 20000) return true;
      delay(10);
    }
  }
}
