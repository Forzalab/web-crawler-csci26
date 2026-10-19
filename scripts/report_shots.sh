#!/usr/bin/env bash
# scripts/report_shots.sh DIR [extra --report flags, e.g. --pdf-link]
#   Screenshots for DIR/summary.pdf (a `wc --report` run dir), then rewrites the PDF with them:
#   DIR/app.jpg      our TUI on the same site (tmux, cache only: .wc-cache in this dir) -> HTML -> Chromium
#   DIR/bridges.jpg  the BRIDGES page from DIR/bridges_url.txt (needs a --upload run) -> Chromium
#   then `$BIN --report URL --bfs|--dfs --depth D --size S --out DIR --offline` (cache replay, live wall time kept).
# Needs tmux, python3, node + Playwright Chromium (cloud has them). A missing tool -> that picture is skipped.
# Env: BIN=./a.out  COLS=160 ROWS=48  WAIT_MS=8000 (BRIDGES draw time)  TIMEOUT=120 (TUI crawl)
set -u
cd "$(dirname "$0")/.." || exit 2
dir=${1:?usage: scripts/report_shots.sh DIR [--pdf-link]}; shift
BIN=${BIN:-./a.out} COLS=${COLS:-160} ROWS=${ROWS:-48} WAIT_MS=${WAIT_MS:-8000} TIMEOUT=${TIMEOUT:-120}
[ -f "$dir/stats.json" ] || { echo "no $dir/stats.json (run wc --report first)"; exit 2; }
read -r url mode depth size < <(python3 -c 'import json,sys; s=json.load(open(sys.argv[1]))
print(s["url"], s["mode"].lower(), s["depth"], s["size"])' "$dir/stats.json")
export NODE_PATH=${NODE_PATH:-$(npm root -g 2> /dev/null)}
shot=0; command -v node > /dev/null && node -e 'require("playwright")' 2> /dev/null && shot=1
[ "$shot" = 1 ] || echo "no node + playwright: no screenshots"
tmp=$(mktemp -d)
trap 'tmux -L "wc-shot-$$" kill-server 2> /dev/null; rm -rf "$tmp"' EXIT

# 1. our app: same crawl in the TUI, from the cache, at COLSxROWS.
if [ "$shot" = 1 ] && command -v tmux > /dev/null; then
    T() { tmux -u -L "wc-shot-$$" -f "$tmp/tmux.conf" "$@"; }
    printf 'set -g status off\nset -g escape-time 0\nset -g default-terminal "tmux-256color"\nset -as terminal-features ",*:RGB"\n' > "$tmp/tmux.conf"
    mkdir -p "$tmp/home"
    T new-session -d -s s -x "$COLS" -y "$ROWS" -c "$PWD" env HOME="$tmp/home" COLORTERM=truecolor LANG=C.UTF-8 "$BIN" --tui
    cap() { T capture-pane -p -t s "$@" 2> /dev/null; }
    until_screen() { for _ in $(seq 1 $(( $2 * 10 ))); do cap | grep -qE "$1" && return 0; sleep 0.1; done; return 1; }
    keys() { T send-keys -t s -l -- "$1"; [ "${2:-}" = enter ] && T send-keys -t s Enter; sleep 0.5; }
    if until_screen 'Paste a URL' 10; then
        keys "$url" enter; keys y; keys "${mode:0:1}"; keys "$depth" enter; keys "$size" enter
        end=$(( $(date +%s) + TIMEOUT ))
        until [ -e "$tmp/home/.wc_tui_seen" ] || [ "$(date +%s)" -ge "$end" ]; do sleep 0.5; done
        prev=""; for _ in $(seq 1 30); do sleep 1; cur=$(cap); [ "$cur" = "$prev" ] && break; prev=$cur; done
        # One Esc drops the coach mark (the omnibar stays: a second Esc would quit).
        cap | grep -q 'Esc skip' && { T send-keys -t s Escape; sleep 0.6; }
        cap -e > "$tmp/app.ansi"
        python3 scripts/ansi_html.py < "$tmp/app.ansi" > "$tmp/app.html" &&
            node scripts/shot.cjs file "$tmp/app.html" "$dir/app.jpg" && echo "app: $dir/app.jpg"
    else
        echo "app: TUI never showed the URL screen"
    fi
    for _ in 1 2 3 4 5; do T send-keys -t s Escape 2> /dev/null; sleep 0.3; done
fi

# 2. BRIDGES: the uploaded graph's page.
if [ "$shot" = 1 ] && [ -s "$dir/bridges_url.txt" ]; then
    node scripts/shot.cjs page "$(head -n 1 "$dir/bridges_url.txt")" "$dir/bridges.jpg" "$WAIT_MS" && echo "bridges: $dir/bridges.jpg"
else
    echo "bridges: no $dir/bridges_url.txt (run wc --report ... --upload first)"
fi

# 3. rewrite summary.pdf with the pictures (cache replay; keeps the live wall time).
"$BIN" --report "$url" --"$mode" --depth "$depth" --size "$size" --out "$dir" --offline "$@" 2> /dev/null
