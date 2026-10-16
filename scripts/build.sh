#!/usr/bin/env bash
# scripts/build.sh [build_dir] - what `make` runs.
# Default: one progress bar, no log spam. Build fails -> big red banner + the
# exact error lines. Full log always saved to <build_dir>/build.log.
# VERBOSE=1 (make V=1) or no terminal (CI) -> stream the full log instead.
set -u
dir=${1:-build}
log="$dir/build.log"
mkdir -p "$dir"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    R=$'\033[1;31m' G=$'\033[1;32m' Y=$'\033[1;33m' C=$'\033[1;36m' D=$'\033[2m' N=$'\033[0m'
else
    R='' G='' Y='' C='' D='' N=''
fi

run() {  # the real build: configure, then compile
    cmake -S . -B "$dir" && cmake --build "$dir" -j
}

start=$(date +%s)
if [ "${VERBOSE:-0}" != 0 ] || [ ! -t 1 ]; then
    run 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
else
    spin='|/-\' i=0 pct=0 what="configuring"
    run > "$log" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        line=$(sed 's/\x1b\[[0-9;]*m//g' "$log" 2>/dev/null | grep -E '^\[ *[0-9]+%\]' | tail -n 1)
        if [ -n "$line" ]; then
            pct=$(sed -E 's/^\[ *([0-9]+)%\].*/\1/' <<< "$line")
            what=$(sed -E 's/^\[ *[0-9]+%\] //; s#.*/##' <<< "$line")
        fi
        fill=$((pct * 30 / 100))
        bar=$(printf '%*s' "$fill" '' | tr ' ' '#')$(printf '%*s' $((30 - fill)) '' | tr ' ' '.')
        printf '\r\033[K%s%s%s [%s%s%s] %3d%%  %s%.50s%s' \
            "$C" "${spin:i++%4:1}" "$N" "$G" "$bar" "$N" "$pct" "$D" "$what" "$N"
        sleep 0.2
    done
    wait "$pid"
    rc=$?
    printf '\r\033[K'
fi
secs=$(( $(date +%s) - start ))

if [ "$rc" -eq 0 ]; then
    echo "${G}BUILD OK${N} ${D}(${secs}s, log: $log)${N}"
    exit 0
fi

echo
echo "${R}#################################################${N}"
echo "${R}##                                             ##${N}"
echo "${R}##          BUILD FAILED - ERROR FOUND         ##${N}"
echo "${R}##                                             ##${N}"
echo "${R}#################################################${N}"
echo
errs=$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -n -E -B2 -A4 ': (fatal )?error:|undefined reference|CMake Error|ld returned' | head -n 60)
if [ -n "$errs" ]; then
    echo "${Y}The error (from $log):${N}"
    echo "$errs" | sed -E "s/(: (fatal )?error:.*|undefined reference.*|CMake Error.*)/${R}\1${N}/"
else
    echo "${Y}No 'error:' line found. Last 30 log lines:${N}"
    tail -n 30 "$log"
fi
echo
echo "${R}BUILD FAILED${N} ${D}(${secs}s). Full log: $log  |  see it live: make V=1${N}"
exit "$rc"
