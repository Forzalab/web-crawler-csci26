# gateway: graph JSON -> BRIDGES

Tiny HTTP function. `wc` builds the graph JSON itself (`wc --bridges`, src/bridges/).
The gateway adds the BRIDGES credentials from **host env**, forwards the JSON to BRIDGES, and returns the visualization URL.
Keys never live in the repo or in the C++ binary. No dependencies (Node ≥18).

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
| ASSIGNMENT_MIN / ASSIGNMENT_MAX | 1 / 99 | lock to the course range |
| MAX_BYTES / MAX_NODES | 4 MB / 1000 | open endpoint guard |
| GATEWAY_TOKEN | unset | if set, callers must send `X-Gateway-Token` |
| TIMEOUT_MS | 15000 | upstream timeout |

```sh
npm test                 # node --test: fake fetch + fake BRIDGES on 127.0.0.1, no network
PORT=8080 BRIDGES_USER=... BRIDGES_KEY=... npm start     # local / Cloud Run
./build/wc --pipe --bridges < tests/crawl/inputfile9 | curl -s --data-binary @- 'localhost:8080/?assignment=26'
```

Files: `gateway.js` (all logic, host-independent) · `server.js` (node:http, Cloud Run/local) · `api/bridges.js` (Vercel).
Not deployed. Deploy waits for Tony's host creds.
