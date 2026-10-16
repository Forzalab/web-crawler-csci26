#!/usr/bin/env bash
# scripts/build.sh [build_dir] - what `make` runs.
# Default: an animated progress bar, no log spam, then a big OK or FAIL banner.
# Build fails -> the exact error lines under the banner. Full log always saved
# to <build_dir>/build.log.
# VERBOSE=1 (make V=1) or no terminal (CI) -> stream the full log instead.
set -u
dir=${1:-build}
log="$dir/build.log"
mkdir -p "$dir"

tty=0; [ -t 1 ] && tty=1
if [ "$tty" = 1 ] && [ -z "${NO_COLOR:-}" ]; then
    R=$'\033[1;31m' G=$'\033[1;32m' Y=$'\033[1;33m' B=$'\033[1;34m' M=$'\033[1;35m'
    C=$'\033[1;36m' D=$'\033[2m' N=$'\033[0m'
else
    R='' G='' Y='' B='' M='' C='' D='' N=''
fi
case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
    *UTF-8*|*utf8*|*UTF8*|*utf-8*) utf=1 ;;
    *) utf=0 ;;
esac

big() {  # big <color> <line>...  : block letters, '#' when the terminal is not UTF-8
    local col=$1; shift
    for l in "$@"; do
        [ "$utf" = 1 ] || l=${l//█/#}
        echo "  ${col}${l}${N}"
    done
}

run() {  # the real build: configure, then compile + link
    cmake -S . -B "$dir" && cmake --build "$dir" -j
}

echo "${B}==>${N} building ${C}wc${N} in ${dir}/   ${D}(make V=1 = full compiler log)${N}"

start=$(date +%s)
if [ "${VERBOSE:-0}" != 0 ] || [ "$tty" = 0 ]; then
    run 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
else
    if [ "$utf" = 1 ]; then
        spin=(⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏) full='█' empty='░' duck=('<(•)' '<(-)')
    else
        spin=('|' '/' '-' '\') full='#' empty='.' duck=('<(o)' '<(-)')
    fi
    i=0 pct=0 stage="configure" what=""
    run > "$log" 2>&1 &
    pid=$!
    printf '\033[?25l'                              # hide the cursor while animating
    trap 'printf "\033[?25h"' EXIT
    trap 'kill "$pid" 2>/dev/null; printf "\r\033[K\033[?25h\n"; exit 130' INT TERM
    while kill -0 "$pid" 2>/dev/null; do
        line=$(sed 's/\x1b\[[0-9;]*m//g' "$log" 2>/dev/null | grep -E '^\[ *[0-9]+%\]' | tail -n 1)
        if [ -n "$line" ]; then
            pct=$(sed -E 's/^\[ *([0-9]+)%\].*/\1/' <<< "$line")
            what=$(sed -E 's/^\[ *[0-9]+%\] //; s#.*/##' <<< "$line")
            case "$what" in
                Linking*) stage="link" ;;
                Built*)   [ "$pct" = 100 ] && stage="done" || stage="compile" ;;
                *)        stage="compile" ;;
            esac
        fi
        case "$stage" in
            configure) sc=$Y ;; compile) sc=$C ;; link) sc=$M ;; *) sc=$G ;;
        esac
        fill=$((pct * 30 / 100))
        bar=""
        for ((k = 0; k < 30; k++)); do
            if ((k < fill)); then bar+=$full; else bar+=$empty; fi
        done
        printf '\r\033[K %s%s%s %s%-9s%s %s%s%s %3d%% %s  %s%.40s%s' \
            "$C" "${spin[i % ${#spin[@]}]}" "$N" "$sc" "$stage" "$N" "$G" "$bar" "$N" \
            "$pct" "${duck[(i / 4) % 2]}" "$D" "$what" "$N"
        i=$((i + 1))
        sleep 0.1
    done
    wait "$pid"
    rc=$?
    printf '\r\033[K\033[?25h'
fi
secs=$(( $(date +%s) - start ))

if [ "$rc" -eq 0 ]; then
    echo
    big "$G" \
        " ██████  ██   ██ ██" \
        "██    ██ ██  ██  ██" \
        "██    ██ █████   ██" \
        "██    ██ ██  ██    " \
        " ██████  ██   ██ ██"
    echo
    echo "  ${G}BUILD OK${N} in ${secs}s.  ${D}next: tests/run.sh   |   log: $log${N}"
    exit 0
fi

echo
big "$R" \
    "███████  █████  ██ ██      ██" \
    "██      ██   ██ ██ ██      ██" \
    "█████   ███████ ██ ██      ██" \
    "██      ██   ██ ██ ██        " \
    "██      ██   ██ ██ ███████ ██"
echo
errs=$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -n -E -B2 -A4 ': (fatal )?error:|undefined reference|CMake Error|ld returned' | head -n 60)
if [ -n "$errs" ]; then
    echo "${Y}ERROR FOUND. Here it is (from $log):${N}"
    echo "$errs" | sed -E "s/(: (fatal )?error:.*|undefined reference.*|CMake Error.*)/${R}\1${N}/"
else
    echo "${Y}No 'error:' line found. Last 30 log lines:${N}"
    tail -n 30 "$log"
fi
echo
echo "${R}BUILD FAILED${N} after ${secs}s.  ${D}The real mistake can be a line or two ABOVE the error line.${N}"
echo "${D}Full log: $log   |   watch it live: make V=1${N}"
exit "$rc"
