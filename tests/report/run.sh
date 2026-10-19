#!/usr/bin/env bash
# tests/report/run.sh [BIN] - offline check of `wc --report` (WC_FIXTURE fake web, no network).
# Crawls tests/crawl/inputfile9 BFS + DFS into a temp dir; checks stats.json fields,
# histogram sums to nodes, failures.tsv rows == failed, graph.json is BRIDGES JSON,
# summary.pdf is a whole PDF (header, %%EOF, startxref -> xref; pdftotext shows host + nodes if installed).
set -u
cd "$(dirname "$0")/../.." || exit 2
BIN=${1:-build/wc}
[ -x "$BIN" ] || { echo "no binary at $BIN (run make first)"; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
for mode in bfs dfs; do
    out=$tmp/$mode
    if ! line=$(WC_FIXTURE=tests/crawl/inputfile9 "$BIN" --report A --$mode --depth 5 --size 10 --out "$out" 2> "$tmp/$mode.err"); then
        echo "FAIL $mode: exit $?"; cat "$tmp/$mode.err"; fail=1; continue
    fi
    if python3 - "$out" "$mode" "$line" <<'PY'
import json, sys
out, mode, line = sys.argv[1:]
s = json.load(open(out + "/stats.json"))
keys = "url mode depth size nodes edges fetches failed blocked blocked_by depth_histogram wall_ms cache started_at replay wall_source".split()
miss = [k for k in keys if k not in s]
assert not miss, f"missing {miss}"
assert s["url"] == "A" and s["mode"] == mode.upper() and s["depth"] == 5 and s["size"] == 10, s
assert sum(s["depth_histogram"].values()) == s["nodes"], "histogram != nodes"
assert 0 < s["nodes"] <= 10 and s["fetches"] > 0 and s["failed"] > 0, s
rows = open(out + "/failures.tsv").read().splitlines()
assert rows[0] == "url\tstatus\terror\tlabel", rows[0]
assert len(rows) - 1 == s["failed"], f"{len(rows) - 1} rows != failed {s['failed']}"
assert all(r.split("\t")[3] == "dead" for r in rows[1:]), rows
g = json.load(open(out + "/graph.json"))
assert g["visual"] == "GraphAdjacencyList" and len(g["nodes"]) == s["nodes"], "graph.json"
assert f"nodes={s['nodes']}" in line and out in line, line
pdf = open(out + "/summary.pdf", "rb").read()
assert pdf.startswith(b"%PDF-1.") and pdf.rstrip().endswith(b"%%EOF"), "summary.pdf header/EOF"
x = int(pdf[pdf.rindex(b"startxref") + 9:].split()[0])
assert pdf[x:x + 4] == b"xref", "startxref does not point at xref"
import shutil, subprocess
if shutil.which("pdftotext"):
    t = subprocess.run(["pdftotext", "-layout", out + "/summary.pdf", "-"], capture_output=True, text=True, check=True).stdout
    assert f"A - {mode.upper()} crawl" in t and str(s["nodes"]) in t, t
PY
    then echo "PASS $mode: $line"; else echo "FAIL $mode"; fail=1; fi
done

# --offline replay over a live run keeps the live wall_ms + started_at (and says so); twice in a row too.
# With no live stats, the replay's own time is labelled "cache replay, not live".
fx() { WC_FIXTURE=tests/crawl/inputfile9 "$BIN" --report A --bfs --depth 5 --size 10 --out "$1" "${@:2}" > /dev/null 2>&1; }
fx "$tmp/rp"
sed -i 's/"wall_ms": [0-9]*/"wall_ms": 110300/; s/"started_at": "[^"]*"/"started_at": "2026-10-19T01:59:05Z"/' "$tmp/rp/stats.json"
fx "$tmp/rp" --offline && fx "$tmp/rp" --offline && fx "$tmp/fresh" --offline
if python3 - "$tmp" <<'PY2'
import json, shutil, subprocess, sys
t = sys.argv[1]
s = json.load(open(t + "/rp/stats.json"))
assert s["replay"] is True and s["wall_source"] == "live", s
assert s["wall_ms"] == 110300 and s["started_at"] == "2026-10-19T01:59:05Z", s
f = json.load(open(t + "/fresh/stats.json"))
assert f["replay"] is True and f["wall_source"] == "replay", f
if shutil.which("pdftotext"):
    txt = lambda d: subprocess.run(["pdftotext", t + d + "/summary.pdf", "-"], capture_output=True, text=True).stdout
    assert "1:50" in txt("/rp") and "live run, 2026-10-19 01:59 UTC" in txt("/rp"), txt("/rp")
    assert "cache replay, not live" in txt("/fresh"), txt("/fresh")
PY2
then echo "PASS replay keeps live wall time"; else echo "FAIL replay"; fail=1; fi
exit $fail
