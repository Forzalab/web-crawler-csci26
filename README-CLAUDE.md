# web-crawler-csci26

CSCI 26 group project: crawl a website, build a graph of its links (BFS and DFS,
max depth 3, max size 1000, 0.1 s between requests) and draw it on BRIDGES.

One binary, `wc`, two front ends:
- **pipe mode** (stdin redirected): reads a fake web from stdin and prints a deterministic report. This is what the tests use. It never touches the network and never really sleeps.
- **TUI** (stdin is a terminal): FTXUI screen. Crawls with 16 parallel fetches (still polite per host: robots.txt, Crawl-delay, 429 pacing; pipe mode stays sequential with the 0.1 s delay). Esc on the loading screen stops and shows the graph so far; `/resume` crawls on into the same graph. `/bridges` uploads the graph through `gateway/` when `WC_GATEWAY_URL` is set (assignment: a random 7-digit id per session, `BRIDGES_ASSIGNMENT` overrides; `WC_BRIDGES_SUB` default 0, `WC_GATEWAY_TOKEN` if the gateway has `GATEWAY_TOKEN`); after that, a finished `/resume` re-uploads to the same assignment, so the same link shows the bigger graph.

TUI graph screen:
- **LINKS** meter = discovered URLs (the graph's nodes), capped by the "Max links" prompt. The loading screen counts `fetched` (attempts), `links`, `fail`, `depth`.
- **Replay**: a finished crawl replays as if live, never longer than 8 s. Small crawls keep one fetch every 120 ms; bigger ones become a wave, one BFS level at a time, siblings landing 3 at a time.
- **Level of detail**, highways first like a map: tree edges into big subtrees first, edges to depth 1 always, SEEN back-links only once every visible tree edge fits the budget (grows with screen size and zoom). Labels go one ring deeper at zoom 2.5 and everywhere at 5. Labels never touch edge dots (padded, one free row above and below).
- **Clusters**: 6+ pages of one other site under the same parent fold into one glyph + `site ×count` while zoom < 2.
- **Focus**: Tab cycles search bar -> preview (if open) -> graph (Tab still completes in the search bar when there is something to complete). The focused part glows orange; the boss-bar rule turns orange for the graph. Preview focused: j/k or arrows scroll, PgUp/PgDn, m max. Graph focused: arrows pan, + - zoom, 0 fit, even with text in the bar. Any other key types into the bar; Esc steps back.
- **/bridges** sends at most 1000 links / 10000 edges: the most prominent ones (root, first hop, most linked-to, with their tree parents), and says so ("1000 of N links"). Its look: our radial layout pinned in a BRIDGES "window" (no force-layout clump), node colour + shape by site, red star root, a thick rainbow path to the deepest page, big top-10 hubs, hover text = title / URL / depth · in-links · status. Pipe `--bridges` keeps the plain format.

## Build

Needs CMake 3.16+ and g++ with C++20 or newer. CMake asks for C++23 and falls back to an older standard if the compiler lacks it.

```sh
make            # = cmake -S . -B build && cmake --build build, with a progress bar. Error -> red banner + the error lines. Log: build/build.log
make V=1        # same, but show the full compiler log live
make lint       # gentle C++ habit tips (using namespace std in headers, NULL, raw new, ...). Never fails the build
make clean
```

`make` also copies the binary to `./a.out` in the repo root: run `./a.out` (same as `build/wc`).

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

## Report crawls

One headless crawl per site + mode, for the report (D2/D3). Same fetch stack as the TUI
(our fetcher + `.wc-cache/` + robots.txt + honest UA), but the spec path: sequential `crawl()`, 0.1 s sleep between requests.

```sh
./a.out --report URL [--bfs|--dfs] [--depth 3] [--size 1000] [--out DIR] [--cache DIR] [--offline] [--upload]
```

- Cache is on: a rerun replays from `.wc-cache/` and never touches the site. `--offline` = cache only.
- Lost or old `summary.pdf`? `./a.out --report URL --bfs --offline` replays the crawl from the cache and writes all files again, PDF included.
- Pictures for the PDF: `scripts/report_shots.sh DIR` (after a `--upload` run). It opens our TUI on the same site from the cache in tmux and screenshots the screen (`app.jpg`), screenshots the BRIDGES page from `bridges_url.txt` (`bridges.jpg`), then rewrites `summary.pdf` by cache replay (live wall time kept). Needs tmux, python3, node + Playwright Chromium (the cloud container has them); a missing one = that box stays empty. Extra flags pass through: `scripts/report_shots.sh DIR --pdf-link`.
- `--upload` posts `graph.json` through the gateway (needs `WC_GATEWAY_URL`, like `/bridges`) and writes `bridges_url.txt`.
- stdout: one summary line. stderr: one line per fetch (`[report N] LABEL cache=hit|miss links=K url`).
- `WC_FIXTURE=file` crawls a fake web instead (`tests/report/run.sh`, offline, in `make test` and CI).

Writes `DIR` (default `runs/<host>-<bfs|dfs>/`, gitignored):

| File | What |
|---|---|
| `stats.json` | url, mode, depth, size, nodes, edges, fetches, failed, blocked (+ `blocked_by` robots / 429 / bot-wall), depth_histogram (sums to nodes), wall_ms, cache hits / misses, started_at, replay (`--offline`), wall_source. A replay over a live run keeps the live wall_ms + started_at (`wall_source: live`; PDF says "live run, <start>"); with no live stats it is `replay` ("cache replay, not live") |
| `failures.tsv` | url, status, error, label: `dead` (404/410/DNS/connect), `robots`, `429`, `redirect-loop`, `tls`, `timeout`, `bot-wall` (403, 202 with no links, JS_SHELL), `other` |
| `graph.json` | `bridges_json(bridges_subset(g))`: at most 1000 nodes, BRIDGES GraphAdjacencyList |
| `summary.pdf` | one printable page (US Letter): site, stats (fetched, failed, nodes, edges, blocked, wall time, cache), then the graph from BRIDGES and from our app (`bridges.jpg`, `app.jpg`; an empty box until `scripts/report_shots.sh` made them). Own tiny PDF writer (`src/ui/report_pdf.cc`), no dependencies |
| `bridges.jpg`, `app.jpg` | `scripts/report_shots.sh DIR` only: screenshots of the BRIDGES page and of our TUI |
| `bridges_url.txt` | `--upload` only: the BRIDGES link |

The 10 report crawls (5 sites x BFS / DFS, spec defaults depth 3, size 1000):

```sh
./a.out --report https://en.wikipedia.org/wiki/Wars_of_the_Roses --bfs
./a.out --report https://en.wikipedia.org/wiki/Wars_of_the_Roses --dfs
./a.out --report "https://www.google.com/search?q=wars+of+the+roses" --bfs
./a.out --report "https://www.google.com/search?q=wars+of+the+roses" --dfs
./a.out --report https://www.espn.com/ --bfs
./a.out --report https://www.espn.com/ --dfs
./a.out --report https://wowcubedocs.com/ --bfs
./a.out --report https://wowcubedocs.com/ --dfs
./a.out --report https://dwarfdig.com/ --bfs
./a.out --report https://dwarfdig.com/ --dfs
```

Polite only: robots.txt is obeyed and bot walls are reported (`blocked`), never worked around.

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
src/core/       graph, LinkSource, Renderer, Sleeper, crawl(), tree (spanning tree)   (no I/O, no deps)
src/core/crawl.cc   #2 traversal: TODO(Tony), currently a stub
src/team/       josh.cc, logan.cc: Josh and Logan write their parts here (plain C++)
src/adapters/   JoshFetcher, LoganRenderer: glue from src/team/ to the crawler (teammates never edit)
src/net/        our fetch path: url, html_links, http (sockets + OpenSSL), HttpsFetcher (redirects), RoutedFetcher, PageCache, robots
src/mocks/      MockFetcher (fake web + A3 parser), MockRenderer (text / DOT)
src/bridges/    bridges_json(): our own BRIDGES graph JSON writer (no bridges-cxx, no post); bridges_subset() for the TUI
src/ui/         pipe_main (tests), tui_main (FTXUI), links_main (--links URL), report_main (--report URL)
src/main.cc     isatty(0) ? TUI : pipe   (--pipe / --tui override)
tests/crawl/    ladder: inputfileN / outputfileN
tests/run.sh    test runner
tests/unit/     unit tests (wc_net_test)
tests/bridges/  golden tests for bridges_json()
tests/report/   offline check of --report (stats.json / failures.tsv / graph.json)
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
