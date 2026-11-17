# PortWatch · AnyPS5 progress tracker

A desk display for the **AnyPS5** PS5-emulator project's reimplementation progress, using the public feed at
`boykopovar.github.io/AnyPS5/` (`progress.json` + `history.json`). The counts measure source code that has been reimplemented. They don't tell you which games run.

| What | File |
|---|---|
| Browser version (monitor, Xeneon Edge, phone) | `PortWatch-Web.html`: open it in any browser. See "Web version" below |
| Board flasher (Chrome/Edge, Mac or Windows) | `PortWatch-Flasher.html`, or `firmware/out/PortWatch-Flasher.html` after building (firmware included) |
| Firmware source (CYD + Guition C/R, one codebase) | `firmware/` |

## Screens
**Boards (CYD and Guition):**
1. **Overview**: ring with % of library functions implemented, functions implemented, gains over 7 and 30 days and the latest day, libraries complete, and the estimate including open PRs.
2. **Last 7 days**: candle chart. On the Guition, **tap the middle** to switch between 7, 14 and 30 days. % implemented runs up the side and the last 7 calendar days run across. Each candle opens at the previous day's % and closes at that day's %: green means it went up, red means it went down (usually because newly *declared* functions raised the total). Blue bars show functions implemented that day.
3. **Daily progress**: the latest day's gain, big, then one row per day with functions implemented and the change in %. Days without commits are shown dimmed.
4. **Libraries**: tap the centre of the screen to change the sort: Moved since the last update / Almost done / Most left / Biggest.
5. **Device info**: IP address, Wi-Fi, signal, board, touch, memory, and when the feed was published. Auto-rotate skips it; double-tap to it or hold the screen for 2 s.

**Web version:** all of the above plus Shader recompiler and Every library (a tile per library).

**Data use:** the board saves the last feed it loaded and shows it straight away at power-on. Each check asks GitHub "has this changed?" and gets a tiny "no" back most of the time. The full ~2.6 MB `progress.json` is only downloaded when AnyPS5 publishes an update.

When a feed update adds functions, the board shows a full-screen **"+312 functions implemented"** for 8 seconds, and the CYD's rear RGB LED pulses green while it's showing (turn that off on the settings page). The rest of the time the LED is off.

**Touch:** **double-tap** the right side for the next screen and the left side for the previous one. A single tap on the sides does nothing, so brushing the screen won't change page. Tap the centre to change the sort on Libraries, or to check the feed now on Overview. Hold for 2 s to see device info (IP, Wi-Fi, memory, last error). Screens auto-cycle every 20 s and pause for a minute after you touch the screen.

**Orientation:** landscape, landscape flipped, portrait or portrait flipped. Set it from the flasher (step 3) or the settings page. Every screen has a portrait layout.

## Web version
- **Edit layout** (top right): drag a tile to move it, drop it on the **middle** of another tile to **stack** them, or on its **edge** to swap. Drag the bottom-right corner to resize. **Remove** puts a panel under "Not on screen", where **+ name** brings it back. **Reset layout** restores the default. The Xeneon Edge strip and normal screens each keep their own saved layout.
- **Stacked tiles** take turns. Dots at the bottom show which panel is up, and tapping a dot switches to it. Turn auto-rotate on or off and set the interval (10 seconds to 1 minute) in **Settings**. Each stack also has its own **Rotate: On/Off** switch in edit mode. Touching or scrolling a tile pauses its rotation for a minute.
- **Expand**: click a panel's title, or double-click it, to bring it up over the dashboard. Esc or a click outside closes it.
- **Credits**: links to the AnyPS5 progress page and GitHub.
- Phones and narrow windows show the tiles in a single column. Layout editing needs a window wider than 1100 px.

## Build the firmware

### GitHub (recommended: nothing to install)
Every push to `main` builds both boards on GitHub's servers (`.github/workflows/build.yml`):
- **Flasher with firmware inside**, online at **https://pulsesys-dev.github.io/PortWatch/**. Open it in Chrome or Edge and flash.
- **Web dashboard** at **https://pulsesys-dev.github.io/PortWatch/web/**
- **All the files** (bins, OTA bins, flasher, package zip) under the run's **Artifacts** in the Actions tab.
- Push a version tag to publish a **release** with the same files. The tag must match `EP_VERSION` in `firmware/PortWatch/config.h`: `git tag b1.0 && git push origin b1.0` makes a beta pre-release, and `v1.0` makes an official release.

One-time setup: in the repo go to **Settings → Pages → Build and deployment → Source**, choose **GitHub Actions**, then re-run the workflow.

### PlatformIO
```
cd firmware
pio run
```
This produces `firmware/out/`:
- `PortWatch-cyd.bin`, `PortWatch-guition.bin`: full images for the browser flasher (address 0x0)
- `PortWatch-*.ota.bin`: app-only files for **Firmware update** on the device's web page
- `PortWatch-Flasher.html`: the flasher with both firmwares baked in, as one file you can hand to people

### Arduino IDE 2
Libraries (Library Manager): **GFX Library for Arduino 1.4.9** (Moon On Our Nation), **ArduinoJson 7.x**, **XPT2046_Touchscreen** (Paul Stoffregen). Core: **esp32 by Espressif 2.0.17** (the same versions LapLogic uses).

| | CYD | Guition JC4827W543 |
|---|---|---|
| Board | ESP32 Dev Module | ESP32S3 Dev Module |
| Partition scheme | Minimal SPIFFS (1.9MB APP with OTA) | Minimal SPIFFS (1.9MB APP with OTA) |
| Flash size | 4MB | 4MB |
| PSRAM | – | **OPI PSRAM** |
| USB CDC On Boot | – | **Enabled** |
| USB Mode | – | Hardware CDC and JTAG |

Use **Sketch → Export Compiled Binary**, rename `build/…/PortWatch.ino.merged.bin` to `PortWatch-cyd.bin` or `PortWatch-guition.bin`, and drop it into the flasher. You can also run `python3 firmware/tools/build_flasher.py` with the files in `firmware/out/` to bake them in.

The sketch picks the board from the chip: ESP32 builds the CYD version and ESP32-S3 builds the Guition version. The Guition auto-detects capacitive (GT911) or resistive (XPT2046) touch, so one binary covers both C and R.

## First boot
1. Open `PortWatch-Flasher.html`, pick your board, then **Connect** (it shows which chip it found and warns if it's the wrong board) and **Flash**. In step 3, **Connect to display** sends your Wi-Fi details, sets the orientation and starts touch calibration, all over USB. You don't need to pick the port again.
   Alternatively, join the **PortWatch-XXXX** network on your phone and pick your Wi-Fi at 192.168.4.1. It must be a 2.4 GHz network.
2. **Resistive screens (CYD, Guition R) need touch calibration.** It starts by itself on first boot. Tap the centre of four crosses (a stylus helps). The last one checks the first three, and it asks you to redo it if they don't line up. Then draw on the screen to check, and tap **Done** or **Redo**. Calibration keeps working if you change orientation. To recalibrate, use **Calibrate touch** in the flasher, the settings page, or hold the screen during the splash.
3. The first load reads the whole ~2.6 MB feed (progress bar on screen, usually 10–30 s). After that, the board only re-downloads when the feed has changed (ETag check, every 30 min by default).

Settings page: **http://portwatch.local** or the IP on the info screen. It covers brightness and night dimming, auto-cycle, which screens are in the rotation, orientation (landscape / flipped), refresh interval, time zone, CYD panel type (ILI9341 or the 2-USB ST7789), invert colours, touch type override, recalibrate touch, forget Wi-Fi, and **firmware update over Wi-Fi** using the `.ota.bin`.

## Notes
- **Not yet run on hardware.** The firmware was compile-checked for both chips against the real library APIs. The feed parser was unit-tested on a full-size `progress.json`, and the screens were rendered to images at 320×240 and 480×272 to check layout. The first real build is still the one on your machine, so if something doesn't compile or the display looks wrong, send me the error or a photo.
- The board streams `progress.json` through a filter and never holds the whole file. Peak memory for parsing is about 40–60 KB plus TLS, which fits the CYD without PSRAM.
- HTTPS to GitHub Pages uses `setInsecure()`, because the feed is public read-only data and this avoids bundling a certificate store.
- `firmware/test/test_model.cpp` is the host test for the parser: `g++ -std=c++17 -I<ArduinoJson>/src -IPortWatch test/test_model.cpp && ./a.out progress.json history.json`.
- Fonts: Adafruit GFX FreeSans (BSD licence, `FONTS-LICENSE.txt`).
