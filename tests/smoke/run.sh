#!/usr/bin/env bash
# tests/smoke/run.sh [filter] [--offline | --live]
#   Drives the REAL TUI (build/wc) in detached tmux panes, one case per line of
#   tests/smoke/cases.tsv, and reports PASS / CRASH / HANG / ERR per case.
#   filter     regex on the case name (e.g. wiki, '^off_dense')
#   --offline  only offline cases (WC_FIXTURE fake web, no network) - what CI runs
#   --live     only live cases (real sites; polite: robots + delay, sequential, .wc-cache reused)
# Env: BIN=build/wc  CASES=tests/smoke/cases.tsv  OUT=build/smoke/<timestamp>
#      SETTLE_MAX=20 (s to wait for the replay to stop changing)  FIXTURE_DELAY_MS=5
# Artifacts: $OUT/<case>/{frames.txt,exit.txt,stderr.txt}, $OUT/summary.tsv;
#            build/smoke/latest -> $OUT.
# Exit 1 if any CRASH or HANG (offline runs: also any ERR, they are deterministic).
set -u
cd "$(dirname "$0")/../.." || exit 2
root=$PWD

filter="" only=""
for a in "$@"; do
    case "$a" in
        --offline) only=offline ;;
        --live) only=live ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) filter=$a ;;
    esac
done

BIN=${BIN:-build/wc}
CASES=${CASES:-tests/smoke/cases.tsv}
SETTLE_MAX=${SETTLE_MAX:-20}
FIXTURE_DELAY_MS=${FIXTURE_DELAY_MS:-5}
stamp=$(date +%Y%m%d-%H%M%S)
OUT=${OUT:-build/smoke/$stamp}
[ -x "$BIN" ] || { echo "no binary at $BIN (run make first)"; exit 2; }
command -v tmux > /dev/null || { echo "tmux not installed (apt-get install tmux)"; exit 2; }
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
ln -sfn "$OUT" build/smoke/latest 2> /dev/null || true

export LANG=C.UTF-8 LC_ALL=C.UTF-8
sock="wc-smoke-$$"
conf="$OUT/tmux.conf"
cat > "$conf" << 'EOF'
set -g remain-on-exit on
set -g status off
set -g escape-time 0
set -g default-terminal "screen-256color"
EOF
T() { tmux -u -L "$sock" -f "$conf" "$@"; }
trap 'T kill-server 2> /dev/null' EXIT

fixdir="$root/build/smoke/fixtures"
if [ "$only" != live ] && grep -qP '\toffline\t' "$CASES"; then
    python3 tests/smoke/gen_fixtures.py "$fixdir" || { echo "fixture generation failed"; exit 2; }
fi

now() { date +%s.%N; }
since() { awk -v a="$1" -v b="$(now)" 'BEGIN { printf "%.1f", b - a }'; }
cap() { T capture-pane -p -t "$sess" 2> /dev/null; }
dead() { [ "$(T display -p -t "$sess" '#{pane_dead}' 2> /dev/null)" = 1 ]; }
frame() {  # frame <label>: append the current screen to frames.txt
    { echo "=== frame: $1 (t=$(since "$t_start")s, $(T display -p -t "$sess" "#{pane_width}x#{pane_height}" 2> /dev/null)) ==="; cap; echo; } >> "$dir/frames.txt"
}
wait_for() {  # wait_for <regex> <seconds>: 0 when the screen matches, 1 on timeout / dead pane
    local end=$(( $(date +%s) + $2 ))
    while [ "$(date +%s)" -lt "$end" ]; do
        cap | grep -qE "$1" && return 0
        dead && return 1
        sleep 0.1
    done
    return 1
}
step() {  # step <keys-literal> <next-screen-regex>: type, Enter, wait for the next screen
    [ -n "$1" ] && T send-keys -t "$sess" -l -- "$1"
    T send-keys -t "$sess" Enter
    if [ "$small" = 1 ]; then sleep 0.4; else wait_for "$2" 5 || true; fi
}
answer() {  # answer <key> <next-screen-regex>: yes/no prompts take the key at once, no Enter
    T send-keys -t "$sess" -l -- "$1"
    if [ "$small" = 1 ]; then sleep 0.4; else wait_for "$2" 5 || true; fi
}
links_of() {  # "LINKS 30/30 LIMIT" from the boss bar (row 1), meter glyphs dropped
    head -n 1 <<< "$1" | grep -oE 'DEPTH [0-9]+/[0-9]+|LINKS [0-9]+/[0-9]+|LIMIT' | paste -sd' ' -
}
layout_check() {  # layout_check <frame text> <small>: "" = ok, else what is wrong
    local f=$1
    grep -q '�' <<< "$f" && { echo "replacement char"; return; }
    if [ "$2" = 1 ]; then
        grep -q 'Enlarge terminal' <<< "$f" || echo "no too-small notice"
        return
    fi
    head -n 1 <<< "$f" | grep -q 'DEPTH' || { echo "row 1 has no DEPTH meter"; return; }
    head -n 1 <<< "$f" | grep -q 'LINKS' || { echo "row 1 has no LINKS meter"; return; }
    [ "$(grep -c 'DEPTH' <<< "$f")" -le 1 ] || echo "boss bar drawn twice"
}

results=() fails=0 n=0
while IFS=$'\t' read -r name url depth size cols rows mode fixture timeout_s crawl; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    [ -n "$only" ] && [ "$mode" != "$only" ] && continue
    [ -n "$filter" ] && ! grep -qE -- "$filter" <<< "$name" && continue
    n=$((n + 1))
    dir="$OUT/$name" home="$OUT/$name/home"
    mkdir -p "$home"
    : > "$dir/frames.txt"
    sess="c$n"
    small=0; { [ "$cols" -lt 80 ] || [ "$rows" -lt 24 ]; } && small=1
    envs=(HOME="$home")
    if [ "$mode" = offline ]; then
        envs+=(WC_FIXTURE="$fixdir/$fixture.txt" WC_FIXTURE_DELAY_MS="$FIXTURE_DELAY_MS")
    fi
    printf '%-26s %-7s %-3s d=%-2s s=%-4s %4sx%-3s ... ' "$name" "$mode" "${crawl:-bfs}" "$depth" "$size" "$cols" "$rows"
    t_start=$(now)
    # fresh HOME: first run every time (Visualize defaults to Y, coach marks on).
    T new-session -d -s "$sess" -x "$cols" -y "$rows" -c "$root" \
        env "${envs[@]}" bash -c '"$0" 2> "$1"; echo $? > "$2"' "$root/$BIN" "$dir/stderr.txt" "$dir/exit.txt"

    verdict="" detail=""
    if ! wait_for 'Paste a URL|Enlarge terminal' 10; then
        verdict=ERR detail="URL screen never shown"
    else
        frame "url"
        T send-keys -t "$sess" -l -- "$url"
        step "" 'Visualize'
        answer "y" 'Mode'
        c=${crawl:-bfs}; answer "${c:0:1}" 'Max depth'  # h / b / d answer at once; Enter alone = Hybrid
        step "$depth" 'Max links'
        step "$size" 'crawling|DEPTH'
        t_crawl=$(now)
        sleep 0.5
        dead || [ -e "$home/.wc_tui_seen" ] || frame "loading"
        end=$(( $(date +%s) + timeout_s ))
        while :; do
            [ -e "$home/.wc_tui_seen" ] && break
            dead && break
            if [ "$(date +%s)" -ge "$end" ]; then
                frame "timeout"
                verdict=HANG detail="crawl not done after ${timeout_s}s"
                break
            fi
            sleep 0.2
        done
        crawl_s=$(since "$t_crawl")
        if [ -z "$verdict" ] && ! dead; then
            sleep 0.3
            frame "replay"
            prev="" waited=0
            while [ "$waited" -lt "$SETTLE_MAX" ] && ! dead; do  # wait for a still frame
                sleep 1; waited=$((waited + 1))
                cur=$(cap)
                [ "$cur" = "$prev" ] && break
                prev=$cur
            done
        fi
        if [ -z "$verdict" ] && ! dead; then
            final=$(cap)
            frame "final"
            detail=$(links_of "$final")
            bad=$(layout_check "$final" "$small")
            if [ -z "$bad" ] && [ "$small" = 1 ]; then  # grow to 120x40: the graph must appear
                T resize-window -t "$sess" -x 120 -y 40
                sleep 1.5
                frame "resized to 120x40"
                bad=$(layout_check "$(cap)" 0)
                [ -n "$bad" ] && bad="after resize: $bad"
                detail=$(links_of "$(cap)")
            fi
            [ -n "$bad" ] && verdict=ERR detail="layout: $bad"
            for _ in 1 2 3 4 5; do  # Esc peels omnibar / coach / preview, then quits
                dead && break
                T send-keys -t "$sess" Escape
                sleep 0.4
            done
            for _ in $(seq 1 30); do dead && break; sleep 0.2; done
            if ! dead; then
                frame "quit"
                verdict=HANG detail="still running after Esc x5"
            fi
        fi
    fi
    dead && [ "$verdict" != HANG ] && frame "exit"
    T kill-session -t "$sess" 2> /dev/null
    sleep 0.2
    rc=$(cat "$dir/exit.txt" 2> /dev/null || echo "none")
    [ -e "$dir/exit.txt" ] || echo "none (killed by runner)" > "$dir/exit.txt"
    if [[ "$rc" =~ ^[0-9]+$ ]] && [ "$rc" -ge 128 ]; then
        sig=$(kill -l $((rc - 128)) 2> /dev/null || echo "?")
        verdict=CRASH detail="exit $rc (SIG$sig)"
    elif grep -qE 'core dumped|Segmentation fault|Aborted|terminate called|AddressSanitizer' "$dir/stderr.txt" "$dir/frames.txt" 2> /dev/null; then
        verdict=CRASH detail="$(grep -hoE 'core dumped|Segmentation fault|Aborted|terminate called.*|AddressSanitizer.*' "$dir/stderr.txt" "$dir/frames.txt" | head -n 1)"
    elif [ -z "$verdict" ] && [ "$rc" != 0 ]; then
        verdict=ERR detail="exit $rc: $(head -c 120 "$dir/stderr.txt" | tr '\n' ' ')"
    elif [ -z "$verdict" ] && [ ! -e "$home/.wc_tui_seen" ]; then
        verdict=ERR detail="exited before the crawl finished"
    fi
    [ -z "$verdict" ] && verdict=PASS
    secs=$(since "$t_start")
    echo "$verdict  ${secs}s  $detail"
    case "$verdict" in
        CRASH|HANG) fails=$((fails + 1)) ;;
        ERR) [ "$mode" = offline ] && fails=$((fails + 1)) ;;
    esac
    results+=("$name"$'\t'"$mode"$'\t'"$depth"$'\t'"$size"$'\t'"${cols}x${rows}"$'\t'"$verdict"$'\t'"$secs"$'\t'"${crawl_s:-}"$'\t'"$detail")
    crawl_s=""
done < "$CASES"

[ "$n" -gt 0 ] || { echo "no cases match '${filter}' ${only:+($only)}"; exit 2; }

{ printf 'case\tmode\tdepth\tsize\tterm\tverdict\tsecs\tcrawl_s\tdetail\n'; printf '%s\n' "${results[@]}"; } > "$OUT/summary.tsv"
echo
echo "SUMMARY ($OUT)"
printf '%-26s %-7s %5s %5s %-7s %-6s %7s  %s\n' case mode depth size term verdict secs detail
for r in "${results[@]}"; do
    IFS=$'\t' read -r c m d s t v sec _ det <<< "$r"
    printf '%-26s %-7s %5s %5s %-7s %-6s %7s  %s\n' "$c" "$m" "$d" "$s" "$t" "$v" "$sec" "$det"
done
counts=$(printf '%s\n' "${results[@]}" | cut -f6 | sort | uniq -c | awk '{printf "%s %s  ", $2, $1}')
echo "TOTAL $n: $counts"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "## TUI smoke${only:+ ($only)}: $counts"
        echo
        echo "| case | mode | depth | size | term | verdict | secs | detail |"
        echo "|---|---|---|---|---|---|---|---|"
        for r in "${results[@]}"; do
            IFS=$'\t' read -r c m d s t v sec _ det <<< "$r"
            echo "| $c | $m | $d | $s | $t | **$v** | $sec | ${det//|/\\|} |"
        done
    } >> "$GITHUB_STEP_SUMMARY"
fi
[ "$fails" -eq 0 ]
