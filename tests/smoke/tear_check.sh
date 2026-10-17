#!/usr/bin/env bash
# tests/smoke/tear_check.sh - the TUI must never draw row by row.
#   Runs build/wc (offline fixture) under strace in tmux panes of fixed sizes, walks the
#   steps, zooms, resizes, quits (Esc; and Ctrl-C), then checks the raw bytes of every
#   write(2) to stdout:
#     - each write is one frame: starts with CSI ?2026h, ends with CSI ?2026l, nothing outside
#     - no CSI 2J (full clear) anywhere
#     - start: alt screen ?1049h, cursor hidden ?25l, mouse ?1000h + ?1006h
#     - exit: ?1049l, ?25h, ?1000l (terminal restored), on Esc and on Ctrl-C
# Needs tmux, strace, python3. Exit 1 on any FAIL; 0 with SKIP when strace is missing.
# Env: BIN=build/wc  OUT=build/smoke/tear
set -u
cd "$(dirname "$0")/../.." || exit 2
root=$PWD
BIN=${BIN:-build/wc}
OUT=${OUT:-build/smoke/tear}
for t in tmux strace python3; do
    command -v $t > /dev/null || { echo "tear_check: SKIP ($t not installed)"
        [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "**Tear check: SKIP** ($t not installed)" >> "$GITHUB_STEP_SUMMARY"
        exit 0; }
done
[ -x "$BIN" ] || { echo "no binary at $BIN (run make first)"; exit 2; }
rm -rf "$OUT" && mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd)
python3 tests/smoke/gen_fixtures.py "$root/build/smoke/fixtures" > /dev/null || exit 2

export LANG=C.UTF-8 LC_ALL=C.UTF-8
sock="wc-tear-$$"
printf 'set -g remain-on-exit on\nset -g status off\nset -g escape-time 0\nset -g default-terminal "screen-256color"\n' > "$OUT/tmux.conf"
T() { tmux -u -L "$sock" -f "$OUT/tmux.conf" "$@"; }
trap 'T kill-server 2> /dev/null' EXIT
dead() { [ "$(T display -p -t "$1" '#{pane_dead}' 2> /dev/null)" = 1 ]; }
waitfor() {  # waitfor <sess> <regex> <s>
    local end=$(( $(date +%s) + $3 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        T capture-pane -p -t "$1" | grep -qE "$2" && return 0
        sleep 0.1
    done
    return 1
}

fails=0
run() {  # run <name> <cols> <rows> <quit: esc|ctrlc>
    local name=$1 cols=$2 rows=$3 quit=$4 dir="$OUT/$1" s=$1
    mkdir -p "$dir/home"
    T new-session -d -s "$s" -x "$cols" -y "$rows" -c "$root" \
        env HOME="$dir/home" WC_FIXTURE="$root/build/smoke/fixtures/star100.txt" WC_FIXTURE_DELAY_MS=5 \
        strace -f -qq -e trace=write -e signal=none -xx -s 4000000 -o "$dir/trace.txt" "$root/$BIN"
    waitfor "$s" 'Paste a URL' 15 || echo "  ($name: URL screen not seen)"
    T send-keys -t "$s" -l -- "https://star.example/"; T send-keys -t "$s" Enter; sleep 0.5
    T send-keys -t "$s" -l y; sleep 0.5
    T send-keys -t "$s" -l 1; T send-keys -t "$s" Enter; sleep 0.5
    T send-keys -t "$s" -l 100; T send-keys -t "$s" Enter
    for _ in $(seq 1 150); do [ -e "$dir/home/.wc_tui_seen" ] && break; sleep 0.2; done
    sleep 1
    for k in + + PPage NPage - 0 Home Left Up; do T send-keys -t "$s" "$k"; sleep 0.15; done
    T resize-window -t "$s" -x $((cols - 10)) -y $((rows - 4)); sleep 0.6
    T resize-window -t "$s" -x "$cols" -y "$rows"; sleep 0.6
    if [ "$quit" = ctrlc ]; then
        T send-keys -t "$s" C-c
    else
        for _ in 1 2 3 4 5; do dead "$s" && break; T send-keys -t "$s" Escape; sleep 0.4; done
    fi
    for _ in $(seq 1 30); do dead "$s" && break; sleep 0.2; done
    dead "$s" || echo "  ($name: still running after quit)"
    T kill-session -t "$s" 2> /dev/null
    printf '%-14s %4sx%-3s quit=%-6s ' "$name" "$cols" "$rows" "$quit"
    python3 - "$dir/trace.txt" << 'EOF' || fails=$((fails + 1))
import re, sys
writes = []
for line in open(sys.argv[1], encoding="ascii", errors="replace"):
    m = re.match(r'\d+\s+write\(1, "((?:\\x[0-9a-f]{2})*)"(\.\.\.)?, (\d+)\) = (-?\d+)', line)
    if m:
        writes.append((bytes.fromhex(m.group(1).replace("\\x", "")), m.group(2), int(m.group(3)), int(m.group(4))))
bad = []
B, E = b"\x1b[?2026h", b"\x1b[?2026l"
for i, (b, trunc, n, ret) in enumerate(writes):
    if trunc or len(b) != n: bad.append(f"write {i}: truncated capture")
    elif ret != n: bad.append(f"write {i}: short write {ret}/{n}")
    elif not (b.startswith(B) and b.endswith(E)) or b.count(B) != 1 or b.count(E) != 1:
        bad.append(f"write {i}: not one bracketed frame ({b[:24]!r}...{b[-12:]!r})")
allb = b"".join(w[0] for w in writes)
if b"\x1b[2J" in allb: bad.append("CSI 2J full clear seen")
if len(writes) < 5: bad.append(f"only {len(writes)} writes")
first = writes[0][0] if writes else b""
start = b"".join(w[0] for w in writes[:3])  # install, terminal queries, first frame
last = b"".join(w[0] for w in writes[-3:])  # last frame, uninstall, final CR
for seq, what in ((b"\x1b[?1049h", "alt screen"), (b"\x1b[?1000h", "mouse"), (b"\x1b[?1006h", "SGR mouse")):
    if seq not in first: bad.append(f"start: no {what} {seq!r}")
if b"\x1b[?25l" not in start: bad.append("start: cursor not hidden")
for seq, what in ((b"\x1b[?1049l", "alt screen off"), (b"\x1b[?25h", "cursor shown"), (b"\x1b[?1000l", "mouse off")):
    if seq not in last: bad.append(f"exit: no {what} {seq!r}")
frames = sum(1 for w in writes if b"\x1b[?25l" in w[0])
if bad:
    print("FAIL  " + "; ".join(bad[:5]) + (f" (+{len(bad)-5} more)" if len(bad) > 5 else "")); sys.exit(1)
print(f"PASS  {len(writes)} writes, {frames} frames, each one write in ?2026h..?2026l, 0x CSI 2J, {len(allb)//1024} KiB")
EOF
}

echo "tear check (raw stdout writes, $BIN)"
run tear_120x40 120 40 esc
run tear_60x30 60 30 esc
run tear_ctrlc 120 40 ctrlc
[ "$fails" -eq 0 ] && verdict="PASS" || verdict="$fails FAIL ($OUT)"
echo "TEAR CHECK: $verdict"
[ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "**Tear check: $verdict**" >> "$GITHUB_STEP_SUMMARY"
[ "$fails" -eq 0 ] || exit 1
