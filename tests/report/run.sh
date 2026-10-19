#!/usr/bin/env bash
# tests/report/run.sh [BIN] - offline check of `wc --report` (WC_FIXTURE fake web, no network).
# Crawls tests/crawl/inputfile9 BFS + DFS into a temp dir; checks stats.json fields,
# histogram sums to nodes, failures.tsv rows == failed, graph.json is BRIDGES JSON.
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
keys = "url mode depth size nodes edges fetches failed blocked blocked_by depth_histogram wall_ms cache started_at".split()
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
PY
    then echo "PASS $mode: $line"; else echo "FAIL $mode"; fail=1; fi
done
exit $fail
