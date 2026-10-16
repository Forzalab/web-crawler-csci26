#!/usr/bin/env bash
# scripts/lint.sh [--summary] - friendly C++ habit check. Never fails the build.
# Greps src/ and tests/ for habits that bite later and prints one tip per hit.
# --summary: print one line ("lint: N tips") for build.sh.  LINT_STRICT=1 -> exit 1 on any hit.
set -u
cd "$(dirname "$0")/.." || exit 0

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    Y=$'\033[1;33m' C=$'\033[1;36m' D=$'\033[2m' N=$'\033[0m'
else
    Y='' C='' D='' N=''
fi

files=$(find src tests -type f \( -name '*.cc' -o -name '*.h' \) 2>/dev/null | sort)
hits=0
out=""

# check <files> <regex> <tip>   (// comments are cut off before matching)
check() {
    local list=$1 re=$2 tip=$3 found
    [ -z "$list" ] && return
    local pat="^[^:]+:[0-9]+:.*($re)"
    [[ $re == ^* ]] && pat="^[^:]+:[0-9]+:${re#^}"   # a ^ in the rule = start of the code line
    found=$(grep -nH '' $list 2>/dev/null | sed -E 's#//.*##' | grep -E "$pat")
    [ -z "$found" ] && return
    hits=$((hits + $(wc -l <<< "$found")))
    out+="${Y}* ${tip}${N}"$'\n'
    out+=$(sed 's/^/    /' <<< "$found")$'\n\n'
}

headers=$(grep -E '\.h$' <<< "$files")
sources=$(grep -E '\.cc$' <<< "$files")

check "$headers" '^[[:space:]]*using[[:space:]]+namespace[[:space:]]+std' \
  "using namespace std; in a HEADER: every file that includes it gets all of std:: dumped in. Name clashes show up far away. Write std::string etc. in headers."
check "$sources" '^[[:space:]]*using[[:space:]]+namespace[[:space:]]+std' \
  "using namespace std; in a .cc: OK for a small file, but std::vector is clearer and never clashes. Your call."
check "$files" '#include[[:space:]]*<bits/stdc\+\+\.h>' \
  "<bits/stdc++.h> is GCC-only and slow to compile. Include what you use (<string>, <vector>, ...)."
check "$files" '(^|[^_[:alnum:]])NULL([^_[:alnum:]]|$)' \
  "NULL is an old C macro. Use nullptr."
check "$files" '(^|[^_[:alnum:]])(new|delete)[[:space:]]+[[:alpha:]_]|delete\[\]' \
  "Raw new/delete: easy to leak. A vector, string or std::unique_ptr cleans up for you."
check "$files" '(^|[^_[:alnum:]])(gets|system)[[:space:]]*\(' \
  "gets() / system() are unsafe. gets has no size limit; system runs a shell."
check "$files" '(^|[^_[:alnum:]])goto[[:space:]]' \
  "goto: a loop, a function or an early return reads easier."
check "$files" '#define[[:space:]]+[A-Z_][A-Z0-9_]*[[:space:]]+[-0-9"]' \
  "#define for a constant: constexpr int / constexpr const char* has a type and a scope."
check "$files" '(^|[^_[:alnum:]])__[[:alnum:]][[:alnum:]_]*[[:alnum:]]([^_[:alnum:]]|$)' \
  "Names starting with __ (or _Capital) are reserved for the compiler and standard library. Pick another name."

for h in $headers; do
    grep -qE '^[[:space:]]*#[[:space:]]*(pragma[[:space:]]+once|ifndef)' "$h" || {
        hits=$((hits + 1))
        out+="${Y}* Header without #pragma once: including it twice redefines everything.${N}"$'\n'"    $h"$'\n\n'
    }
done

if [ "${1:-}" = --summary ]; then
    [ "$hits" -gt 0 ] && echo "  ${D}lint: $hits gentle tip(s) about C++ habits. See them: make lint${N}"
    exit 0
fi

if [ "$hits" -eq 0 ]; then
    echo "${C}<(•) lint: no tips. Clean habits. Nice.${N}"
    exit 0
fi
echo "${C}<(•) lint: $hits tip(s). None of these break the build. They are habits that bite later.${N}"
echo
printf '%s' "$out"
echo "${D}Questions about a tip? Ask in the team chat. Nobody gets graded on this list.${N}"
[ "${LINT_STRICT:-0}" != 0 ] && exit 1
exit 0
