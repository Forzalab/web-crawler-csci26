# web-crawler-csci26

CSCI 26 group project: crawl a website, build a graph of its links (BFS and DFS,
max depth 3, max size 1000, 0.1 s between requests) and draw it on BRIDGES.

One binary, `wc`, two front ends:
- **pipe mode** (stdin redirected): reads a fake web from stdin and prints a deterministic report. This is what the tests use. It never touches the network and never really sleeps.
- **TUI** (stdin is a terminal): FTXUI screen. Placeholder for now (press `q` to quit); the layout comes later.

## Build

Needs CMake 3.16+ and g++ with C++20 or newer. CMake asks for C++23 and falls back to an older standard if the compiler lacks it.

```sh
make            # = cmake -S . -B build && cmake --build build, with a progress bar. Error -> red banner + the error lines. Log: build/build.log
make V=1        # same, but show the full compiler log live
make lint       # gentle C++ habit tips (using namespace std in headers, NULL, raw new, ...). Never fails the build
make clean
```

## Run (pipe mode)

```sh
./build/wc < tests/crawl/inputfile9            # A3 report on stdout
./build/wc --stats --verbose < tests/crawl/inputfile9   # + telemetry on stderr
./build/wc --dot < tests/crawl/inputfile9 | dot -Tpng -o g.png
./build/wc --bridges < tests/crawl/inputfile9          # BRIDGES graph JSON (no post)
```

Input: `MODE START MAXDEPTH MAXSIZE`, then `P`, then P lines `url k link1 .. linkk`
(`k = -1` means the fetch fails; a url that is not listed also fails).
Output: `MODE`, `FETCH`, `NODE` and `EDGE` lines, then the `NODES` / `EDGES` / `FETCHES` / `FAILED` / `SLEEPS` counts.

## Real fetch (smoke test)

```sh
./build/wc --links https://en.wikipedia.org/wiki/Wars_of_the_Roses   # links on stdout
./build/wc --links http://wowcubedocs.com/                           # 308 -> https, followed
```

stderr shows `[fetch] via=tls status=... redirects=... final=... error=...`.
`https://` always goes to our fetcher (`src/net/`); `http://` goes to Josh's adapter first and falls back to ours if it fails.
Needs OpenSSL dev headers for https (`apt install libssl-dev`); without them it still builds and https fetches fail with `tls: built without OpenSSL`.
Certificates are verified against the system CA store (`SSL_CERT_FILE=...` points OpenSSL at another bundle).

Every fetch is cached on disk (`.wc-cache/`, gitignored), so reruns stay off live sites and report crawls replay exactly:

```sh
./build/wc --links URL --offline     # cache only, never the network ("offline: not cached" on a miss)
./build/wc --links URL --refresh     # fetch live and overwrite the cached copy
./build/wc --links URL --cache DIR   # other cache dir;  --no-cache = off
```

One file per URL: a short text header (status, final URL, error, fetched_at) then the HTML body (format in `src/net/cache.h`).
Network failures (DNS, connect, timeout) are not cached, so the next run retries them.

Polite by default:
- **robots.txt** is read once per site (RFC 9309: our `CSCI26-WebCrawler` group, else `*`). A disallowed page is never requested and gets the label `ROBOTS`. robots.txt 4xx = allow all; 5xx = disallow all. `--no-robots` turns this off.
- **429 / 503 + Retry-After**: wait (header value, else 1 s then 2 s, max 30 s) and retry up to 2 times. After that the host stays paced (max 5 s before each request). A `Crawl-delay:` in robots.txt paces the same way.
- **Status label** per page (`label=` on stderr): `OK`, `ROBOTS`, `HTTP_404`, `TLS`, `TIMEOUT`, `DNS`, `CONNECT`, `REDIRECTS`, `NOT_HTML`, `OFFLINE`, `JS_SHELL` (page loaded but has no links and mentions JavaScript), ... (`status_label()` in `src/net/https_fetcher.h`).

## Tests

```sh
make unit       # build/wc_net_test (URL / link / HTTP / redirect / routing, no network) + BRIDGES JSON golden tests
make test       # unit, then the ladder: tests/run.sh [X [Y]] [build/wc] [tests/crawl] [timeout_s]
tests/run.sh 3  # only inputfile3.  tests/run.sh 0 5 = inputfile0..5
tests/run.sh dfs  # only DFS-mode crawl tests (bfs = BFS). Combines: tests/run.sh dfs 0 9
build/test_bridges_json tests/bridges/golden            # BRIDGES JSON golden files
build/test_bridges_json tests/bridges/golden --update   # rewrite them after a format change
```

Same rules as Kerney's `input_tester`: `wc < inputfileN`, `diff -w` against `outputfileN`
(the diff is shown with `icdiff` if it is installed), 2 s timeout, and each test runs twice to check the output is deterministic.
Prints PASS or FAIL per rung and a summary. CI runs the same script on every push and PR; it only reports and never blocks a merge.

## Folder map

```
src/core/       graph, LinkSource, Renderer, Sleeper, crawl()   (no I/O, no deps)
src/core/crawl.cc   #2 traversal: TODO(Tony), currently a stub
src/team/       josh.cc, logan.cc: Josh and Logan write their parts here (plain C++)
src/adapters/   JoshFetcher, LoganRenderer: glue from src/team/ to the crawler (teammates never edit)
src/net/        our fetch path: url, html_links, http (sockets + OpenSSL), HttpsFetcher (redirects), RoutedFetcher, PageCache, robots
src/mocks/      MockFetcher (fake web + A3 parser), MockRenderer (text / DOT)
src/bridges/    bridges_json(): our own BRIDGES graph JSON writer (no bridges-cxx, no post)
src/ui/         pipe_main (tests), tui_main (FTXUI), links_main (--links URL)
src/main.cc     isatty(0) ? TUI : pipe   (--pipe / --tui override)
tests/crawl/    ladder: inputfileN / outputfileN
tests/run.sh    test runner
tests/unit/     unit tests (wc_net_test)
tests/bridges/  golden tests for bridges_json()
third_party/ftxui   FTXUI v7.0.3, vendored, MIT (see its LICENSE)
```

## Who owns what

| Part | Owner | Where it plugs in |
|---|---|---|
| #1 fetch a page, return its URLs | Josh | `src/team/josh.cc`: `josh_get_links(url)` |
| #2 graph traversal, BFS + DFS | Tony | `src/core/crawl.cc` |
| #3 BRIDGES rendering | Logan | `src/team/logan.cc`: `logan_draw(nodes, edges)` |

Josh and Logan each fill in one function in `src/team/`, using plain `vector`, `string` and small structs. The `.h` files there say exactly what goes in and what comes out. The glue in `src/adapters/` turns those into the crawler's types, so nobody else's code needs to change.

Secrets: copy `.env.example` to `.env` (gitignored). Never commit keys.
