#!/usr/bin/env bash
# tests/report/teammate_img.sh [BIN] - offline check of scripts/report_shots.sh --bridges-img / --app-img.
# A PNG (converted) for BRIDGES and a JPEG (copied) for our app go into summary.pdf as 2 images with
# "(teammate)" captions. Both boxes are swapped, so no TUI/BRIDGES screenshots are taken.
# Neither python3 PIL nor node + playwright: BRIDGES gets the JPEG too (PNG conversion SKIPped).
set -u
cd "$(dirname "$0")/../.." || exit 2
BIN=${1:-build/wc}
[ -x "$BIN" ] || { echo "no binary at $BIN (run make first)"; exit 2; }
export NODE_PATH=${NODE_PATH:-$(npm root -g 2> /dev/null)}
conv=1  # no PIL and no Chromium: JPEG only (copied, needs no converter); the PNG case is skipped
python3 -c 'import PIL' 2> /dev/null || node -e 'require("playwright")' 2> /dev/null || conv=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export WC_FIXTURE=tests/crawl/inputfile9
"$BIN" --report A --bfs --depth 5 --size 10 --out "$tmp/r" > /dev/null 2>&1 || { echo "FAIL teammate_img: wc --report"; exit 1; }
# 4x4 red RGB PNG, no PIL.
python3 - "$tmp/x.png" <<'PY'
import struct, sys, zlib
chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
raw = b"".join(b"\x00" + b"\xff\x00\x00" * 4 for _ in range(4))
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
PY
python3 -c 'import sys; open(sys.argv[1], "wb").write(b"\xff\xd8\xff\xc0\x00\x11\x08\x00\x10\x00\x20\x03" + b"\x01" * 9 + b"\xff\xd9")' "$tmp/y.jpg"
[ "$conv" = 1 ] && b=$tmp/x.png || { b=$tmp/y.jpg; echo "SKIP teammate_img PNG: no python3 PIL and no node + playwright"; }
BIN=$BIN scripts/report_shots.sh "$tmp/r" --bridges-img "$b" --app-img "$tmp/y.jpg" > "$tmp/shots.log" 2>&1
if python3 - "$tmp" <<'PY'
import sys
t = sys.argv[1]
pdf = open(t + "/r/summary.pdf", "rb").read()
assert pdf.count(b"/Subtype /Image") == 2, "2 images in summary.pdf"
assert b"(BRIDGES \\(teammate\\))" in pdf and b"(Graph \\(teammate\\))" in pdf, "teammate captions"
assert open(t + "/r/bridges.jpg", "rb").read(2) == b"\xff\xd8", "bridges.jpg is not a JPEG"
assert open(t + "/r/app.jpg", "rb").read() == open(t + "/y.jpg", "rb").read(), "app.jpg not copied as-is"
PY
then echo "PASS teammate_img: $(basename "$b") + y.jpg embedded with teammate captions"; else echo "FAIL teammate_img"; cat "$tmp/shots.log"; exit 1; fi
