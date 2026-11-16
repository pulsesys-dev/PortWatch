#!/usr/bin/env python3
"""Bakes the built firmware into a single-file browser flasher.

Reads out/PortWatch-cyd.bin and/or out/PortWatch-guition.bin (merged images from
`pio run`, or Arduino IDE's "Export Compiled Binary" *.merged.bin renamed to match)
and writes out/PortWatch-Flasher.html. Open that file in Chrome/Edge and flash — no server needed.
Usage: python3 tools/build_flasher.py [path/to/out]
"""
import base64, json, os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "out")
tpl = open(os.path.join(here, "flasher_template.html"), encoding="utf-8").read()
ver = "b1.0"
cfg = os.path.join(root, "PortWatch", "config.h")
if os.path.exists(cfg):
    m = re.search(r'#define EP_VERSION "([^"]+)"', open(cfg, encoding="utf-8").read())
    if m: ver = m.group(1)

blocks, found = [], []
for board in ("cyd", "guition"):
    p = os.path.join(out, f"PortWatch-{board}.bin")
    if os.path.exists(p):
        data = open(p, "rb").read()
        blocks.append(f'<script type="application/octet-stream" id="fw-{board}">{base64.b64encode(data).decode()}</script>')
        found.append(f"{board} ({len(data)//1024} KB)")
blocks.append(f'<script type="application/json" id="fw-meta">{json.dumps({"version": ver})}</script>')
lib = os.path.join(here, "esptool.bundle.js")   # esptool-js 0.7.0 (Apache-2.0), the version the LapLogic flasher uses, so flashing works offline
if os.path.exists(lib):
    blocks.append(f'<script type="text/plain" id="esptool-src">{base64.b64encode(open(lib, "rb").read()).decode()}</script>')
html = tpl.replace("<!--FIRMWARE-->", "\n".join(blocks))
os.makedirs(out, exist_ok=True)
dest = os.path.join(out, "PortWatch-Flasher.html")
open(dest, "w", encoding="utf-8").write(html)
print("PortWatch: flasher written to", dest, "with", ", ".join(found) if found else "no firmware (file picker only)")
