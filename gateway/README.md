# gateway: graph JSON -> BRIDGES

Tiny HTTP function. `wc` builds the graph JSON itself (`wc --bridges`, src/bridges/).
The gateway adds the BRIDGES credentials from **host env**, forwards the JSON to BRIDGES, and returns the visualization URL.
Keys never live in the repo or in the C++ binary. Node ≥18; `/api/bridges` has no dependencies.

```
POST /?assignment=N[&sub=K]          (Vercel: POST /api/bridges?assignment=N[&sub=K])
body: GraphAdjacencyList JSON
200 {"ok":true,"url":"http://assignments.bridgesuncc.org/assignments/N/<user>","assignment":N,"sub":K}
4xx/5xx {"ok":false,"error":"..."}   (never contains the key or BRIDGES' reply body)
```

| env | default | |
|---|---|---|
| BRIDGES_USER, BRIDGES_KEY | none (required) | host secret store only |
| BRIDGES_SERVER | http://assignments.bridgesuncc.org | |
| ASSIGNMENT_MIN / ASSIGNMENT_MAX | 1 / 99999999 | 8 digits (BRIDGES takes big ids, e.g. 69696969); lock to the course range if you like |
| MAX_BYTES / MAX_NODES | 4 MB / 1000 | open endpoint guard. The TUI never trips it: `/bridges` sends a subset of at most 1000 nodes / 10000 edges (`bridges_subset`) |
| GATEWAY_TOKEN | unset | if set, callers must send `X-Gateway-Token` |
| TIMEOUT_MS | 15000 | upstream timeout |

```sh
npm test                 # node --test: fake fetch + fake BRIDGES on 127.0.0.1, no network
PORT=8080 BRIDGES_USER=... BRIDGES_KEY=... npm start     # local / Cloud Run
./build/wc --pipe --bridges < tests/crawl/inputfile9 | curl -s --data-binary @- 'localhost:8080/?assignment=26'
```

## 24 h PDF links (`wc --report ... --pdf-link`)

```
POST /api/pdf?name=<host>-<mode>     body = raw summary.pdf (starts %PDF-, max 2 MB), X-Gateway-Token if GATEWAY_TOKEN is set
200 {"ok":true,"url":"https://<host>/api/pdf?id=<id>","expires_at":"<ISO>"}
GET /api/pdf?id=<id>                 200 application/pdf inline, Cache-Control max-age = seconds left
                                     410 after 24 h (and deleted) · 404 unknown id
GET /api/pdf-sweep                   daily Vercel cron (vercel.json): deletes stored PDFs past 24 h
```

- id = `<expiry unix s>-<32 hex>`: 128-bit random, the expiry needs no lookup. Stored at `pdf/<id>/<name>.pdf`.
- Storage: Vercel Blob store `wc-pdf-links` connected to the project (`BLOB_READ_WRITE_TOKEN`, set by Vercel). The only dependency (`@vercel/blob`, pinned), loaded by `api/` only; `pdf.js` + tests use an in-memory store. `server.js` serves `/api/pdf` from memory (local runs).
- Visible at most 24 h; stored at most ~48 h (cron runs once a day). The raw blob URL is never returned.
- Optional `CRON_SECRET`: then only Vercel's cron may call `/api/pdf-sweep`. `PUBLIC_URL` overrides the link base (default: the request host).

The TUI posts here on `/bridges` (`WC_GATEWAY_URL=<this endpoint>`, optional `WC_BRIDGES_SUB`, `WC_GATEWAY_TOKEN`).
Assignment: a random 7-digit id picked once per TUI session (every upload and the `/resume` re-upload reuse it, so one link updates in place); `BRIDGES_ASSIGNMENT=N` overrides it.

Files: `gateway.js` (all logic, host-independent) · `pdf.js` (24 h PDF links) · `server.js` (node:http, Cloud Run/local) · `api/bridges.js`, `api/pdf.js`, `api/pdf-sweep.js`, `blob_store.js`, `vercel.json` (Vercel).
Deployed: `https://wc-bridges-gateway.vercel.app/api/bridges` (Vercel project `wc-bridges-gateway`, team `forzalabs-projects`, root dir `gateway/`, no build; production domain public, previews behind Vercel Authentication).
Secrets BRIDGES_USER, BRIDGES_KEY, GATEWAY_TOKEN live only in Vercel → wc-bridges-gateway → Settings → Environment Variables (Production, Sensitive). Changing one needs a redeploy (Deployments → latest Production → ⋯ → Redeploy).
Without them the endpoint answers 500 `gateway has no BRIDGES credentials`.
