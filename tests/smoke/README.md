# TUI smoke suite

Drives the real TUI (`build/wc`) inside detached tmux panes of fixed sizes, types
the URL / Visualize / Mode / depth / links prompts (Mode: the first letter of the
case's optional last `crawl` column in `cases.tsv`: `b` (default `bfs`), `d` for `dfs`, `h` for `hybrid`; Enter alone = Hybrid), waits for the crawl and the replay, then
quits with Esc. Catches crashes, hangs and broken layouts. Needs `tmux` and `python3`.

## Run

```
make smoke-offline              # fixture graphs, no network (what CI runs, blocking)
make smoke-live                 # real sites, local only
make smoke                      # everything
make smoke FILTER=wiki          # regex on case names
tests/smoke/run.sh '^off_dense' --offline
```

Exit code is 1 if any case is CRASH or HANG (offline runs: ERR too).

`make smoke-offline` also runs `tests/smoke/tear_check.sh` (needs `strace`; SKIP without):
the TUI under strace at 120x40, 60x30 and a Ctrl-C quit. Every `write(2)` to stdout must be
one whole frame inside `CSI ?2026h` ... `CSI ?2026l` (synchronized output, `src/ui/term_lock.h`),
no `CSI 2J`, alt screen + hidden cursor + mouse on at start, all restored on exit.
Raw traces: `build/smoke/tear/<case>/trace.txt`.

It also runs `tests/smoke/stop_resume.sh`: a slow offline crawl (tree1111, 400 ms per fetch),
Esc on the loading screen must show the graph within 2 s with fewer than 1000 links, `/resume`
must reach `LINKS 1000/1000`, Esc x5 exits 0; Ctrl-C mid-crawl must exit with the alt screen off.
With a fake `gateway/` on 127.0.0.1: no upload without `/bridges`; `/bridges` after the stop uploads
once, and the finished `/resume` uploads again (same assignment + sub, 1000 nodes).

## Verdicts

| verdict | meaning |
|---|---|
| PASS  | crawl finished, final screen has the boss bar (`DEPTH` / `LINKS` on row 1), Esc quit with exit 0 |
| CRASH | killed by a signal (exit >= 128), or `core dumped` / `terminate called` / `Segmentation fault` in stderr or the pane |
| HANG  | crawl not done within the case timeout, or the app did not exit after Esc x5 |
| ERR   | non-zero exit, URL screen never shown, or layout check failed (`detail` says which) |

Terminals under 80x24 show the "Enlarge terminal" notice; for those the runner checks the
notice, then resizes the pane to 120x40 and checks the graph screen appears.

"Crawl finished" is detected by `$HOME/.wc_tui_seen` appearing (each case runs with a
fresh `HOME`, so every run is a first run: Visualize defaults to Y, coach marks show).

## Results

`build/smoke/<timestamp>/` (`build/smoke/latest` points at the newest run):

- `summary.tsv` - one row per case
- `<case>/frames.txt` - screen captures: `url`, `loading`, `replay`, `final`
  (+ `resized to 120x40`, `timeout`, `quit`, `exit` when they apply)
- `<case>/exit.txt` - exit code of `wc` (`none` = killed by the runner)
- `<case>/stderr.txt` - stderr of `wc`

## Add a case

One TAB-separated line in `tests/smoke/cases.tsv`:

```
name  url  depth  size  cols  rows  live|offline  fixture  timeout_s
```

- live: `fixture` is `-`. The crawl is polite (robots.txt, 100 ms delay, cases run one at
  a time) and reuses `.wc-cache/` in the repo root, so reruns do not hit the site again.
  Delete `.wc-cache/` to fetch fresh. Keep `size` <= 1000.
- offline: `fixture` is a name written by `gen_fixtures.py` (A3 pipe format; regenerated
  into `build/smoke/fixtures/` on every run, fixed seed). `url` must be that fixture's start
  URL. Add a new graph shape there. The TUI reads it via `WC_FIXTURE=<file>`
  (`src/ui/fixture_source.cc`); `WC_FIXTURE_DELAY_MS` (runner: `FIXTURE_DELAY_MS`, default 5)
  slows each fake fetch so the loading screen gets captured.
