#!/usr/bin/env bash
# tests/smoke/stop_resume.sh - Esc stops a crawl, /resume finishes it, Ctrl-C quits mid-crawl.
#   Offline (tree1111 fixture, slow fetches), real TUI in tmux, 120x40, a fake gateway/ on
#   127.0.0.1 (WC_GATEWAY_URL) that logs every upload:
#     stop:  Esc on the loading screen -> graph screen within 2 s with PAGES n/1000, n < 1000
#            -> /resume -> PAGES 1000/1000 -> Esc x5 exits 0; never /bridges -> 0 uploads
#     share: stop -> /bridges (1 upload, n nodes) -> /resume -> "BRIDGES updated" (2nd upload,
#            same assignment + sub, 1000 nodes)
#     ctrlc: Ctrl-C on the loading screen -> exits, alt screen off (terminal restored)
# Exit 1 on any FAIL. Env: BIN=build/wc  OUT=build/smoke/stop_resume  DELAY_MS=400
set -u
cd "$(dirname "$0")/../.." || exit 2
root=$PWD
BIN=${BIN:-build/wc}
OUT=${OUT:-build/smoke/stop_resume}
DELAY_MS=${DELAY_MS:-400}
for t in tmux python3; do command -v $t > /dev/null || { echo "stop_resume: $t not installed"; exit 2; }; done
[ -x "$BIN" ] || { echo "no binary at $BIN (run make first)"; exit 2; }
rm -rf "$OUT" && mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd)
python3 tests/smoke/gen_fixtures.py "$root/build/smoke/fixtures" > /dev/null || exit 2

export LANG=C.UTF-8 LC_ALL=C.UTF-8
# Fake gateway: every POST -> one line "<path?query> <nodes>" in $OUT/<session>.posts, 200 + a BRIDGES url.
python3 -c '
import http.server, json, sys
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        sess = self.path.split("sess=")[1].split("&")[0]
        with open(sys.argv[1] + "/" + sess + ".posts", "a") as f:
            f.write(self.path + " " + str(len(json.loads(body)["nodes"])) + "\n")
        out = json.dumps({"ok": True, "url": "http://assignments.bridgesuncc.org/assignments/26/tony"}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)
    def log_message(self, *a): pass
s = http.server.HTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1] + "/gateway.port", "w").write(str(s.server_port))
s.serve_forever()' "$OUT" &
gw_pid=$!
for _ in $(seq 1 50); do [ -s "$OUT/gateway.port" ] && break; sleep 0.1; done
gw="http://127.0.0.1:$(cat "$OUT/gateway.port" 2> /dev/null)/?sess="
posts() { [ -e "$OUT/$1.posts" ] && wc -l < "$OUT/$1.posts" || echo 0; }
sock="wc-stop-$$"
printf 'set -g remain-on-exit on\nset -g status off\nset -g escape-time 0\nset -g default-terminal "screen-256color"\n' > "$OUT/tmux.conf"
T() { tmux -u -L "$sock" -f "$OUT/tmux.conf" "$@"; }
trap 'T kill-server 2> /dev/null; kill $gw_pid 2> /dev/null' EXIT
cap() { T capture-pane -p -t "$1" 2> /dev/null; }
dead() { [ "$(T display -p -t "$1" '#{pane_dead}' 2> /dev/null)" = 1 ]; }
waitfor() {  # waitfor <sess> <regex> <seconds>
    local end=$(( $(date +%s%N) / 1000000 + $3 * 1000 ))
    while [ $(( $(date +%s%N) / 1000000 )) -lt "$end" ]; do
        cap "$1" | grep -qE "$2" && return 0
        dead "$1" && return 1
        sleep 0.1
    done
    return 1
}
pages() { cap "$1" | head -n 1 | grep -oE 'PAGES [0-9]+/[0-9]+'; }
start() {  # start <sess>: through the prompts, crawl begins (depth 3, 1000 pages)
    mkdir -p "$OUT/$1/home"
    T new-session -d -s "$1" -x 120 -y 40 -c "$root" \
        env HOME="$OUT/$1/home" WC_FIXTURE="$root/build/smoke/fixtures/tree1111.txt" WC_FIXTURE_DELAY_MS="$DELAY_MS" \
        WC_GATEWAY_URL="$gw$1" BRIDGES_ASSIGNMENT=26 WC_BRIDGES_SUB=3 \
        bash -c '"$0" 2> "$1"; echo $? > "$2"' "$root/$BIN" "$OUT/$1/stderr.txt" "$OUT/$1/exit.txt"
    waitfor "$1" 'Paste a URL' 10 || return 1
    T send-keys -t "$1" -l -- "https://tree.example/"; T send-keys -t "$1" Enter; waitfor "$1" 'Visualize' 5
    T send-keys -t "$1" -l y; waitfor "$1" 'Max depth' 5
    T send-keys -t "$1" -l 3; T send-keys -t "$1" Enter; waitfor "$1" 'Max pages' 5
    T send-keys -t "$1" -l 1000; T send-keys -t "$1" Enter
    waitfor "$1" 'crawling' 5
}
fails=0
verdict() {  # verdict <name> <problem or "">
    if [ -z "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: $2"; fails=$((fails + 1)); fi
    cap "$1" > "$OUT/$1/last_screen.txt"
}

echo "stop / resume smoke ($BIN, ${DELAY_MS} ms per fetch)"

# stop -> graph [-> /bridges] -> /resume -> full graph [-> re-upload] -> quit
stop_resume() {  # stop_resume <sess> <share: 0|1>
    local s=$1 share=$2 bad="" prev="" cur="" n rc
    start $s || bad="loading screen never shown"
    if [ -z "$bad" ]; then
        sleep 1
        T send-keys -t $s Escape
        waitfor $s 'DEPTH' 2 || bad="no graph within 2 s of Esc"
    fi
    if [ -z "$bad" ]; then
        waitfor $s 'stopped · [0-9]+ left' 2 || bad="no 'stopped · N left' toast"
        local same=0
        for _ in $(seq 1 60); do  # replay: wait until the meter holds still for 2 s
            cur=$(pages $s)
            if [ -n "$cur" ] && [ "$cur" = "$prev" ]; then same=$((same + 1)); else same=0; fi
            [ "$same" -ge 4 ] && break
            prev=$cur; sleep 0.5
        done
        n=$(grep -oE '[0-9]+/' <<< "$cur" | tr -d /)
        echo "      $s after Esc: $cur"
        { [ -n "$n" ] && [ "$n" -lt 1000 ]; } || bad="${bad:-PAGES after stop: '$cur' (want n/1000, n < 1000)}"
    fi
    if [ -z "$bad" ] && [ "$share" = 1 ]; then
        T send-keys -t $s -l "/bridges"; T send-keys -t $s Enter
        waitfor $s 'assignments/26/tony' 5 || bad="no BRIDGES link after /bridges"
        grep -q "assignment=26&sub=3 $n\$" "$OUT/$s.posts" 2> /dev/null || bad="${bad:-upload: $(cat "$OUT/$s.posts" 2> /dev/null) (want 1 POST, assignment=26&sub=3, $n nodes)}"
        T send-keys -t $s Escape; sleep 0.3
    fi
    if [ -z "$bad" ]; then
        T send-keys -t $s -l "/resume"; T send-keys -t $s Enter
        waitfor $s 'PAGES 1000/1000' 30 || bad="PAGES 1000/1000 never shown after /resume ($(pages $s))"
        echo "      $s after /resume: $(pages $s)"
    fi
    if [ -z "$bad" ] && [ "$share" = 1 ]; then
        waitfor $s 'BRIDGES updated' 5 || bad="no 'BRIDGES updated' toast"
        [ "$(posts $s)" = 2 ] && tail -n 1 "$OUT/$s.posts" | grep -q "assignment=26&sub=3 1000$" ||
            bad="${bad:-re-upload: $(tr '\n' ';' < "$OUT/$s.posts") (want a 2nd POST, same assignment+sub, 1000 nodes)}"
    fi
    [ -z "$bad" ] && [ "$share" = 0 ] && [ "$(posts $s)" != 0 ] && bad="uploaded without /bridges"
    for _ in 1 2 3 4 5; do dead $s && break; T send-keys -t $s Escape; sleep 0.4; done
    for _ in $(seq 1 30); do dead $s && break; sleep 0.2; done
    rc=$(cat "$OUT/$s/exit.txt" 2> /dev/null || echo none)
    [ -z "$bad" ] && { dead $s || bad="still running after Esc x5"; }
    [ -z "$bad" ] && [ "$rc" != 0 ] && bad="exit $rc"
    verdict $s "$bad"
}
stop_resume stop 0
stop_resume share 1

# Ctrl-C mid-crawl: quits, terminal restored
s=ctrlc bad=""
start $s || bad="loading screen never shown"
if [ -z "$bad" ]; then
    sleep 1
    T send-keys -t $s C-c
    for _ in $(seq 1 30); do dead $s && break; sleep 0.1; done
    dead $s || bad="still running 3 s after Ctrl-C"
    [ -z "$bad" ] && [ "$(T display -p -t $s '#{alternate_on}')" != 0 ] && bad="alt screen still on"
    grep -qE 'core dumped|Segmentation|terminate called' "$OUT/$s/stderr.txt" && bad="crash: $(head -c 120 "$OUT/$s/stderr.txt")"
fi
verdict $s "$bad"

[ "$fails" -eq 0 ] && echo "STOP/RESUME: PASS" || echo "STOP/RESUME: $fails FAIL ($OUT)"
[ -n "${GITHUB_STEP_SUMMARY:-}" ] && { [ "$fails" -eq 0 ] && echo "**Stop/resume smoke: PASS**" || echo "**Stop/resume smoke: $fails FAIL**"; } >> "$GITHUB_STEP_SUMMARY"
[ "$fails" -eq 0 ]
