#!/usr/bin/env bash
# spec1_check.sh - static check of a spec #1 implementation (no compiler, sed/grep only).
#
# Spec #1: "write a function in C++ with networking code that will pull a website,
# using readlib and tcpstreams (boost beast) and return a vector with a list of
# strings containing the URLs". The professor's demo:
#     #include <boost/beast/core/tcp_stream.hpp>
#     #include "/public/read.h"
#     boost::asio::ip::tcp::iostream stream(hostname, "http");
#     print(stream, "GET {} HTTP/1.1\r\nHost: {}\r\n...", path, hostname);
#     string line = readline(stream);
#
# Usage: tests/team/spec1_check.sh [--func NAME] FILE [FILE...]   (default FILE: src/team/josh.cc)
#
# Comments and string literals are removed first, so an include or a call that is
# only commented out or quoted does not count. The only thing kept from strings is
# whether a literal starts with "GET " (for S5).
#
# Allowed: reusing our parsing helpers net/url.h and net/html_links.h (parse_url,
# extract_links). Not allowed: doing the networking through our fetch code (S6).
#
# Exit 0 = no FAIL in any file. S7 (timeout) is only a WARN.

func=josh_get_links
files=()
while [ $# -gt 0 ]; do
    case "$1" in
        --func) func="$2"; shift 2 ;;
        --func=*) func="${1#--func=}"; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) files+=("$1"); shift ;;
    esac
done
[ ${#files[@]} -eq 0 ] && files=(src/team/josh.cc)

# Print FILE without comments and string/char literals.
# 1) #include "x" -> #include <x> (only real include lines, at line start)
# 2) "GET ..." literals -> __GET_LITERAL__, every other string/char literal -> ""
# 3) // and /* */ comments -> space (whole file at once, leftmost match wins)
strip() {
    sed -E \
        -e '/^[[:space:]]*#[[:space:]]*include[[:space:]]*"/ s/"([^"]*)"/<\1>/' \
        -e 's/"GET ([^"\\]|\\.)*"/ __GET_LITERAL__ /g' \
        -e "s/\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'/\"\"/g" "$1" |
    sed -z -E 's#//[^\n]*|/\*([^*]|\*+[^*/])*\*+/# #g'
}

overall_fail=0
for f in "${files[@]}"; do
    if [ ! -r "$f" ]; then
        echo "$f: cannot read"; overall_fail=1; continue
    fi
    code=$(strip "$f")
    pass=0 warn=0 fail=0
    res() {  # res ID STATUS REASON
        printf '  %-3s %-4s %s\n' "$1" "$2" "$3"
        case "$2" in PASS) pass=$((pass+1)) ;; WARN) warn=$((warn+1)) ;; *) fail=$((fail+1)) ;; esac
    }
    has() { grep -Eq "$1" <<<"$code"; }

    echo "$f:"

    # S1 boost beast / asio header
    if has '^[[:space:]]*#[[:space:]]*include[[:space:]]*<boost/(beast|asio)[^>]*>'; then
        res S1 PASS "includes a boost beast/asio header"
    else
        res S1 FAIL "no #include <boost/beast/...> or <boost/asio...>"
    fi

    # S2 boost tcp stream. Type names: tcp::iostream, tcp_stream, plus `using X = ...` aliases.
    types='tcp::iostream|tcp_stream'
    aliases=$(grep -Eo "using[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[^;]*($types)" <<<"$code" |
              sed -E 's/using[[:space:]]+([A-Za-z_][A-Za-z0-9_]*).*/\1/' | sort -u | paste -sd'|')
    [ -n "$aliases" ] && types="$types|$aliases"
    streams=$(grep -Eo "(^|[^A-Za-z0-9_])($types)[[:space:]]*[&*]?[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*[({;=,)]" <<<"$code" |
              sed -E 's/[[:space:]]*[({;=,)]$//; s/.*[^A-Za-z0-9_]([A-Za-z_][A-Za-z0-9_]*)$/\1/' |
              grep -Evx "tcp_stream|iostream|${aliases:-#}" | sort -u | paste -sd'|')
    if [ -n "$streams" ]; then
        res S2 PASS "opens a boost tcp stream ($streams)"
    elif has "($types)"; then
        res S2 FAIL "names a tcp stream type but never declares a stream"
    else
        res S2 FAIL "no boost tcp::iostream or beast::tcp_stream"
    fi

    # S3 readlib include
    if has '^[[:space:]]*#[[:space:]]*include[[:space:]]*<([^>]*/)?read\.h>'; then
        res S3 PASS "includes readlib (read.h)"
    else
        res S3 FAIL "no #include of read.h"
    fi

    # S4 readlib called on the stream
    if [ -n "$streams" ] && has "(^|[^A-Za-z0-9_:.>])(readline|read)[[:space:]]*(<[^>]*>)?[[:space:]]*\([[:space:]]*\*?[[:space:]]*($streams)\b"; then
        res S4 PASS "reads the response with readlib on the stream"
    elif has '(^|[^A-Za-z0-9_:.>])readline[[:space:]]*\('; then
        res S4 FAIL "calls readline() but not on a boost tcp stream"
    else
        res S4 FAIL "never calls readline()/read() on the stream"
    fi

    # S5 writes a "GET ..." request line (directly, or via a variable that is then written)
    if has '__GET_LITERAL__'; then
        getvars=$(grep -Eo '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(=|\(|\{)[^;]*__GET_LITERAL__' <<<"$code" |
                  sed -E 's/^([A-Za-z_][A-Za-z0-9_]*).*/\1/' |
                  grep -Evx 'print|println|format|write|send|fprintf' | sort -u | paste -sd'|')
        if has '(<<|print|println|write|send|format)[^;]*__GET_LITERAL__' ||
           { [ -n "$getvars" ] && has "(<<[[:space:]]*($getvars)\b|(print|println|write|send)[[:space:]]*\([^;]*\b($getvars)\b)"; }; then
            res S5 PASS "writes a GET request line to the stream"
        else
            res S5 FAIL "has a \"GET \" string but never writes it to a stream"
        fi
    else
        res S5 FAIL "never sends a \"GET ...\" request"
    fi

    # S6 networking not done through our code (net/url.h and net/html_links.h are fine)
    ours=$( { grep -Eo '^[[:space:]]*#[[:space:]]*include[[:space:]]*<([^>]*/)?net/(http|https_fetcher|cache|routed[^>]*)\.h>' <<<"$code" |
              sed -E 's/.*<([^>]*)>.*/\1/'
              grep -Eo '\b(HttpClient|HttpResponse|HttpOptions|parse_http_response|HttpsFetcher|RoutedFetcher|FetchResult|PageCache)\b' <<<"$code"
            } | sort -u | paste -sd' ')
    if [ -z "$ours" ]; then
        res S6 PASS "none of our fetch code (net/http.h, HttpClient, ...)"
    else
        res S6 FAIL "networking goes through our code: $ours"
    fi

    # S7 timeout on the stream (WARN only: the demo has none, josh_test case 11 needs one)
    if has '\b(expires_after|expires_from_now|expires_at|SO_RCVTIMEO)\b'; then
        res S7 PASS "sets a timeout on the stream"
    else
        res S7 WARN "no timeout (expires_after): a server that never answers hangs it (josh_test case 11)"
    fi

    # S8 defines FUNC and fills / returns the links
    if grep -Ezq "(^|[^A-Za-z0-9_:])$func[[:space:]]*\([^;{}]*\)[[:space:]]*(const[[:space:]]*)?\{" <<<"$code"; then
        if has '(\.links[[:space:]]*(=[^=]|\.(push_back|emplace_back|insert|assign|swap))|(push_back|emplace_back)[[:space:]]*\(|return[[:space:]]*\{)'; then
            res S8 PASS "defines $func and fills the links"
        else
            res S8 FAIL "defines $func but never fills the links"
        fi
    else
        res S8 FAIL "no definition of $func"
    fi

    echo "spec #1: $pass/8 PASS, $warn WARN, $fail FAIL"
    [ "$fail" -gt 0 ] && overall_fail=1
done
exit $overall_fail
