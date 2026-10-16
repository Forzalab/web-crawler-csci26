#!/usr/bin/env bash
# tests/run.sh [binary=build/wc] [testdir=tests/crawl] [timeout_s=2]
# Same rules as Kerney's input_tester: `binary < inputfileX`, stdout only,
# compared whitespace-insensitive (diff -w) with outputfileX, 2 s timeout,
# run TWICE (output must be deterministic). Files numbered from 0, no gaps.
# Exit 1 if anything fails. If $GITHUB_STEP_SUMMARY is set, a table goes there.
set -u
bin=${1:-build/wc}
dir=${2:-tests/crawl}
limit=${3:-2}

[ -x "$bin" ] || { echo "no binary at $bin (run make first)"; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass=0 fail=0 rows=""
x=0
while [ -e "$dir/inputfile$x" ]; do
    in="$dir/inputfile$x" want="$dir/outputfile$x"
    timeout -s TERM "$limit" "$bin" < "$in" > "$tmp/out1" 2>/dev/null
    rc=$?
    timeout -s TERM "$limit" "$bin" < "$in" > "$tmp/out2" 2>/dev/null
    if [ $rc -eq 124 ] || [ $rc -eq 137 ]; then
        verdict="FAIL (TIMED OUT)"
    elif ! cmp -s "$tmp/out1" "$tmp/out2"; then
        verdict="FAIL (NONDETERMINISTIC)"
    elif [ ! -e "$want" ]; then
        verdict="FAIL (missing $want)"
    elif diff -w -q "$tmp/out1" "$want" > /dev/null; then
        verdict="PASS"
    else
        verdict="FAIL"
    fi
    echo "Test $x : $verdict"
    if [ "$verdict" = PASS ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        if [ "$verdict" = FAIL ]; then   # show the diff (icdiff if installed, like Kerney)
            shown=0
            if command -v icdiff > /dev/null; then
                # some icdiff builds crash (Python traceback); fall back to plain diff then
                icdiff -L "yours" -L "expected" "$tmp/out1" "$want" > "$tmp/d" 2> "$tmp/e"
                [ -s "$tmp/e" ] || { sed 's/^/    /' "$tmp/d"; shown=1; }
            fi
            [ "$shown" = 1 ] || diff -w "$tmp/out1" "$want" | sed 's/^/    /'
        fi
    fi
    rows+="| $x | $verdict |"$'\n'
    x=$((x + 1))
done
for extra in "$dir"/inputfile*; do   # a gap silently skips files in Kerney's loop
    n=${extra##*inputfile}
    [ "$n" -ge "$x" ] 2>/dev/null && echo "WARNING: gap before inputfile$n (not run)"
done

summary="$pass passed, $fail failed, $x total ($dir)"
echo "SUMMARY: $summary"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    { echo "## crawl ladder: $summary"; echo; echo "| rung | result |"; echo "|---|---|"; printf '%s' "$rows"; } >> "$GITHUB_STEP_SUMMARY"
fi
[ "$fail" -eq 0 ]
