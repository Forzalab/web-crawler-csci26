# web-crawler-csci26

CSCI 26 group project: crawl a website, build a graph of its links (BFS and DFS,
max depth 3, max size 1000, 0.1 s between requests) and draw it on BRIDGES.

One binary, `wc`, two front ends:
- **pipe mode** (stdin redirected): reads a fake web from stdin and prints a deterministic report. This is what the tests use. It never touches the network and never really sleeps.
- **TUI** (stdin is a terminal): FTXUI screen. Placeholder for now (press `q` to quit); the layout comes later.

## Build

Needs CMake 3.16+ and g++ with C++20 or newer. CMake asks for C++23 and falls back to an older standard if the compiler lacks it.

```sh
make            # = cmake -S . -B build && cmake --build build
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

## Tests

```sh
make test       # or: tests/run.sh [build/wc] [tests/crawl] [timeout_s]
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
src/adapters/   JoshFetcher, LoganRenderer: teammate code is wired here only (// WIRE:)
src/mocks/      MockFetcher (fake web + A3 parser), MockRenderer (text / DOT)
src/bridges/    bridges_json(): our own BRIDGES graph JSON writer (no bridges-cxx, no post)
src/ui/         pipe_main (tests), tui_main (FTXUI)
src/main.cc     isatty(0) ? TUI : pipe   (--pipe / --tui override)
tests/crawl/    ladder: inputfileN / outputfileN
tests/run.sh    test runner
tests/bridges/  golden tests for bridges_json()
third_party/ftxui   FTXUI v7.0.3, vendored, MIT (see its LICENSE)
```

## Who owns what

| Part | Owner | Where it plugs in |
|---|---|---|
| #1 fetch a page, return its URLs | Josh | `src/adapters/josh_fetcher.cc` (`// WIRE:`) |
| #2 graph traversal, BFS + DFS | Tony | `src/core/crawl.cc` |
| #3 BRIDGES rendering | Logan | `src/adapters/logan_renderer.cc` (`// WIRE:`) |

Each teammate's own function signature goes only inside their adapter, next to a `// WIRE:` comment. The rest of the code only sees `LinkSource` and `Renderer`.

Secrets: copy `.env.example` to `.env` (gitignored). Never commit keys.
