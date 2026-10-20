#!/usr/bin/env bash
# spec1_selftest.sh - runs spec1_check.sh on the samples in spec1_samples/ and compares
# every check with the ground truth below. No compiler, no network. In `make test`.
# Usage: tests/team/spec1_selftest.sh [-q]     (-q: only print failures and the summary)

here=$(cd "$(dirname "$0")" && pwd)
check="$here/spec1_check.sh"
samples="$here/spec1_samples"
quiet=0
[ "${1:-}" = "-q" ] && quiet=1

# sample        S1   S2   S3   S4   S5   S6   S7   S8   exit
truth=(
"good.cc        PASS PASS PASS PASS PASS PASS PASS PASS 0"
"demo_style.cc  PASS PASS PASS PASS PASS PASS WARN PASS 0"
"aliases.cc     PASS PASS PASS PASS PASS PASS PASS PASS 0"
"calls_us.cc    FAIL FAIL FAIL FAIL FAIL FAIL WARN PASS 1"
"fake_include.cc FAIL FAIL FAIL FAIL FAIL FAIL WARN PASS 1"
"stub.cc        FAIL FAIL FAIL FAIL FAIL PASS WARN FAIL 1"
)

bad=0
for row in "${truth[@]}"; do
    read -r name e1 e2 e3 e4 e5 e6 e7 e8 eexit <<<"$row"
    want=("$e1" "$e2" "$e3" "$e4" "$e5" "$e6" "$e7" "$e8")
    out=$("$check" "$samples/$name")
    code=$?
    ok=1
    for i in 1 2 3 4 5 6 7 8; do
        got=$(sed -nE "s/^  S$i +(PASS|WARN|FAIL) .*/\1/p" <<<"$out")
        if [ "$got" != "${want[$((i-1))]}" ]; then
            echo "FAIL $name S$i: want ${want[$((i-1))]}, got ${got:-nothing}"
            ok=0
        fi
    done
    if [ "$code" != "$eexit" ]; then
        echo "FAIL $name: want exit $eexit, got $code"
        ok=0
    fi
    if [ $ok = 1 ]; then
        [ $quiet = 1 ] || echo "PASS $name"
    else
        bad=$((bad+1))
        echo "$out"
    fi
done

if [ $bad -eq 0 ]; then
    echo "spec1 selftest: ${#truth[@]}/${#truth[@]} samples match"
    exit 0
fi
echo "spec1 selftest: $bad of ${#truth[@]} samples do not match"
exit 1
