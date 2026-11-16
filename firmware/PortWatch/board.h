// Display, backlight, touch and LED for each supported board.
//
//  ESP32 (classic)  -> ESP32-2432S028R "Cheap Yellow Display" 2.8" 320x240, ILI9341 or ST7789, XPT2046 resistive touch
//  ESP32-S3         -> Guition JC4827W543 4.3" 480x272 NV3041A (QSPI)
//                      C version = GT911 capacitive touch, R version = XPT2046 resistive touch (auto-detected)
#pragma once
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"

#if CONFIG_IDF_TARGET_ESP32S3
  #define EP_GUITION 1
  #define EP_BOARD   "Guition JC4827W543"
  #define EP_BL_PIN  1
#elif CONFIG_IDF_TARGET_ESP32
  #define EP_CYD     1
  #define EP_BOARD   "CYD ESP32-2432S028R"
  #define EP_BL_PIN  21
  #define EP_LED_R   4
  #define EP_LED_G   16
  #define EP_LED_B   17
#else
  #error "PortWatch supports the ESP32 (CYD) and ESP32-S3 (Guition JC4827W543) only"
#endif

Arduino_DataBus *bus = nullptr;
Arduino_GFX     *panel = nullptr;
Arduino_Canvas  *canvas = nullptr;   // Guition: full-frame buffer in PSRAM, no flicker
Arduino_GFX     *g = nullptr;        // what the UI draws on
int16_t W = 320, H = 240;            // current screen size (swapped in portrait)
#if EP_GUITION
const int16_t LW = 480, LH = 272;    // landscape size: touch is worked out in these coordinates
const uint8_t PANEL_ROT[4] = {0, 2, 3, 1};   // landscape, landscape flipped, portrait, portrait flipped
#else
const int16_t LW = 320, LH = 240;
const uint8_t PANEL_ROT[4] = {1, 3, 0, 2};
#endif
inline bool isPortrait() { return S.rotation >= 2; }

// landscape coordinates <-> what's on screen in the chosen orientation
void landToScreen(int16_t lx, int16_t ly, int16_t &sx, int16_t &sy) {
  switch (S.rotation) {
    case 1: sx = LW - 1 - lx; sy = LH - 1 - ly; break;
    case 2: sx = LH - 1 - ly; sy = lx; break;
    case 3: sx = ly; sy = LW - 1 - lx; break;
    default: sx = lx; sy = ly;
  }
}
void screenToLand(int16_t sx, int16_t sy, int16_t &lx, int16_t &ly) {
  switch (S.rotation) {
    case 1: lx = LW - 1 - sx; ly = LH - 1 - sy; break;
    case 2: lx = sy; ly = LH - 1 - sx; break;
    case 3: lx = LW - 1 - sy; ly = sx; break;
    default: lx = sx; ly = sy;
  }
}

bool displayBegin() {
#if EP_GUITION
  bus = new Arduino_ESP32QSPI(45 /*CS*/, 47 /*SCK*/, 21 /*D0*/, 48 /*D1*/, 40 /*D2*/, 39 /*D3*/);
  panel = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true /*IPS*/);
  canvas = new Arduino_Canvas(480, 272, panel, 0, 0, PANEL_ROT[S.rotation]);
  if (canvas->begin()) {
    g = canvas;
  } else {                                   // no PSRAM? draw straight to the panel
    delete canvas; canvas = nullptr;
    if (!panel->begin()) return false;
    panel->setRotation(PANEL_ROT[S.rotation]);
    g = panel;
  }
#else
  bus = new Arduino_ESP32SPI(2 /*DC*/, 15 /*CS*/, 14 /*SCK*/, 13 /*MOSI*/, 12 /*MISO*/, HSPI);
  uint8_t rot = PANEL_ROT[S.rotation];
  if (S.cydPanel == 1) panel = new Arduino_ST7789(bus, GFX_NOT_DEFINED, rot, true, 240, 320);
  else                 panel = new Arduino_ILI9341(bus, GFX_NOT_DEFINED, rot);
  if (!panel->begin(40000000)) return false;
  g = panel;
#endif
  if (S.invert) panel->invertDisplay(true);
  W = g->width(); H = g->height();
  g->fillScreen(0x0000);
  return true;
}

inline void flush() { if (canvas) canvas->flush(); }

// ---------- backlight ----------
void blBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(EP_BL_PIN, 5000, 8);
#else
  ledcSetup(0, 5000, 8); ledcAttachPin(EP_BL_PIN, 0);
#endif
}
uint8_t blNow = 255;
void blSet(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == blNow) return;
  blNow = pct;
  uint32_t duty = pct == 0 ? 0 : map(pct, 1, 100, 6, 255);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(EP_BL_PIN, duty);
#else
  ledcWrite(0, duty);
#endif
}

// ---------- CYD rear RGB LED (active low) ----------
void ledBegin() {
#if EP_CYD
  pinMode(EP_LED_R, OUTPUT); pinMode(EP_LED_G, OUTPUT); pinMode(EP_LED_B, OUTPUT);
  digitalWrite(EP_LED_R, HIGH); digitalWrite(EP_LED_G, HIGH); digitalWrite(EP_LED_B, HIGH);
#endif
}
void ledSet(bool r, bool gr, bool b) {
#if EP_CYD
  digitalWrite(EP_LED_R, r ? LOW : HIGH); digitalWrite(EP_LED_G, gr ? LOW : HIGH); digitalWrite(EP_LED_B, b ? LOW : HIGH);
#else
  (void)r; (void)gr; (void)b;
#endif
}

// ---------- touch ----------
enum TouchKind : uint8_t { TOUCH_NONE, TOUCH_GT911, TOUCH_XPT };
TouchKind touchKind = TOUCH_NONE;

// Resistive calibration: an affine map from raw touch readings to landscape coordinates
//   lx = a*rx + b*ry + c,  ly = d*rx + e*ry + f
// Worked out from tapped targets, so it handles swapped/mirrored axes, and it stays valid when
// you change orientation (orientation is applied afterwards).
struct Cal { uint32_t magic; float a, b, c, d, e, f; };
Cal cal;
const uint32_t CAL_MAGIC = 0x9C0A1B03;

SPIClass *tsSpi = nullptr;
XPT2046_Touchscreen *ts = nullptr;

#if EP_GUITION
uint8_t gtAddr = 0;
bool gtWriteReg(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gtAddr); Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(v);
  return Wire.endTransmission() == 0;
}
bool gtReadReg(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(gtAddr); Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)gtAddr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
bool gtProbe() {
  // reset with INT low -> address 0x5D
  pinMode(38, OUTPUT); pinMode(3, OUTPUT);
  digitalWrite(3, LOW); digitalWrite(38, LOW); delay(12);
  digitalWrite(38, HIGH); delay(12);
  pinMode(3, INPUT); delay(60);
  Wire.begin(8, 4); Wire.setClock(400000);
  const uint8_t addrs[2] = {0x5D, 0x14};
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { gtAddr = a; return true; }
  }
  Wire.end();
  return false;
}
bool gtDown = false; int16_t gtX = 0, gtY = 0;
bool gtRead(int16_t &x, int16_t &y) {
  uint8_t st;
  if (gtReadReg(0x814E, &st, 1) && (st & 0x80)) {
    uint8_t n = st & 0x0F;
    if (n > 0 && n <= 5) {
      uint8_t b[4];
      if (gtReadReg(0x8150, b, 4)) { gtX = b[0] | (b[1] << 8); gtY = b[2] | (b[3] << 8); gtDown = true; }
    } else gtDown = false;
    gtWriteReg(0x814E, 0);
  }
  x = gtX; y = gtY;
  return gtDown;
}
#endif

void calDefaults() {   // rough guess until the screen is calibrated
  cal.magic = 0;
  cal.a = LW / 3500.0f; cal.b = 0; cal.c = -250 * cal.a;
  cal.d = 0; cal.e = LH / 3500.0f; cal.f = -250 * cal.e;
}
bool calLoad() {
  prefs.begin("portwatch", true);
  size_t n = prefs.getBytes("cal", &cal, sizeof(cal));
  prefs.end();
  if (n != sizeof(cal) || cal.magic != CAL_MAGIC) { calDefaults(); return false; }
  return true;
}
void calSave() {
  cal.magic = CAL_MAGIC;
  prefs.begin("portwatch", false); prefs.putBytes("cal", &cal, sizeof(cal)); prefs.end();
}
void calClear() { prefs.begin("portwatch", false); prefs.remove("cal"); prefs.end(); calDefaults(); }

void touchBegin() {
#if EP_GUITION
  bool tryCap = S.touchMode != 2, tryRes = S.touchMode != 1;
  if (tryCap && gtProbe()) { touchKind = TOUCH_GT911; Serial.printf("Touch: GT911 at 0x%02X\n", gtAddr); return; }
  if (tryRes) {
    tsSpi = new SPIClass(HSPI);                 // display QSPI is on SPI2, touch gets SPI3
    tsSpi->begin(12 /*SCK*/, 13 /*MISO*/, 11 /*MOSI*/, 38 /*CS*/);
    ts = new XPT2046_Touchscreen(38, 3);
    ts->begin(*tsSpi);
    ts->setRotation(0);
    touchKind = TOUCH_XPT; Serial.println("Touch: XPT2046 (resistive)");
  }
#else
  tsSpi = new SPIClass(VSPI);
  tsSpi->begin(25 /*SCK*/, 39 /*MISO*/, 32 /*MOSI*/, 33 /*CS*/);
  ts = new XPT2046_Touchscreen(33, 36);
  ts->begin(*tsSpi);
  ts->setRotation(0);
  touchKind = TOUCH_XPT;
#endif
  calLoad();
}

bool touchRaw(int16_t &rx, int16_t &ry) {
  if (touchKind != TOUCH_XPT || !ts->touched()) return false;
  TS_Point p = ts->getPoint();
  if (p.z < 250) return false;
  rx = p.x; ry = p.y;
  return true;
}

// Returns true while pressed; x/y in current screen coordinates.
bool touchRead(int16_t &x, int16_t &y) {
  int16_t px, py;
  if (touchKind == TOUCH_XPT) {
    int16_t rx, ry;
    if (!touchRaw(rx, ry)) return false;
    px = (int16_t)(cal.a * rx + cal.b * ry + cal.c); py = (int16_t)(cal.d * rx + cal.e * ry + cal.f);
  }
#if EP_GUITION
  else if (touchKind == TOUCH_GT911) {
    if (!gtRead(px, py)) return false;
  }
#endif
  else return false;
  px = constrain(px, 0, LW - 1); py = constrain(py, 0, LH - 1);   // landscape coordinates so far
  landToScreen(px, py, x, y);
  return true;
}
bool touchIsResistive() { return touchKind == TOUCH_XPT; }
const char *touchName() { return touchKind == TOUCH_GT911 ? "capacitive (GT911)" : touchKind == TOUCH_XPT ? "resistive (XPT2046)" : "none"; }
