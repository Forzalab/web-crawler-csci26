// gateway.js - the whole gateway, host-independent. No dependencies.
//   handle({method, query, headers, body}, env, fetch) -> {status, json}
// Wrapped by server.js (Cloud Run / local) and api/bridges.js (Vercel).
// Credentials come from env only and never appear in a response or a log.

const DEFAULTS = {
  BRIDGES_SERVER: 'http://assignments.bridgesuncc.org', // bridges-cxx "live" (http only)
  ASSIGNMENT_MIN: '1',
  ASSIGNMENT_MAX: '99',
  MAX_BYTES: String(4 * 1024 * 1024), // 1000 nodes + 20k edges ~ 1.7 MB (A11a §2)
  MAX_NODES: '1000',                   // spec max size
  TIMEOUT_MS: '15000',
};

const fail = (status, error) => ({ status, json: { ok: false, error } });

function intIn(s, lo, hi) {
  if (!/^\d+$/.test(String(s ?? ''))) return null;
  const n = Number(s);
  return n >= lo && n <= hi ? n : null;
}

export async function handle(req, env = {}, fetchImpl = fetch) {
  const cfg = { ...DEFAULTS, ...env };
  if (req.method !== 'POST') return fail(405, 'POST only');

  // Optional shared token: set GATEWAY_TOKEN on the host and send X-Gateway-Token.
  if (cfg.GATEWAY_TOKEN && req.headers?.['x-gateway-token'] !== cfg.GATEWAY_TOKEN)
    return fail(401, 'bad gateway token');

  if (!cfg.BRIDGES_USER || !cfg.BRIDGES_KEY) return fail(500, 'gateway has no BRIDGES credentials');

  const assn = intIn(req.query?.assignment, Number(cfg.ASSIGNMENT_MIN), Number(cfg.ASSIGNMENT_MAX));
  if (assn === null) return fail(400, `assignment must be ${cfg.ASSIGNMENT_MIN}..${cfg.ASSIGNMENT_MAX}`);
  const sub = req.query?.sub === undefined ? 0 : intIn(req.query.sub, 0, 98); // bridges-cxx stops at 99
  if (sub === null) return fail(400, 'sub must be 0..98');

  const body = typeof req.body === 'string' ? req.body : '';
  if (Buffer.byteLength(body) > Number(cfg.MAX_BYTES)) return fail(413, `body over ${cfg.MAX_BYTES} bytes`);

  let graph;
  try { graph = JSON.parse(body); } catch { return fail(400, 'body is not JSON'); }
  if (graph?.visual !== 'GraphAdjacencyList' || !Array.isArray(graph.nodes) || !Array.isArray(graph.links))
    return fail(400, 'not a GraphAdjacencyList (visual/nodes/links)');
  if (graph.nodes.length > Number(cfg.MAX_NODES)) return fail(413, `more than ${cfg.MAX_NODES} nodes`);

  const base = cfg.BRIDGES_SERVER.replace(/\/+$/, '');
  const nn = String(sub).padStart(2, '0');
  const user = encodeURIComponent(cfg.BRIDGES_USER);
  const post = `${base}/assignments/${assn}.${nn}?apikey=${encodeURIComponent(cfg.BRIDGES_KEY)}&username=${user}`;

  let r;
  try {
    r = await fetchImpl(post, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body,
      signal: AbortSignal.timeout(Number(cfg.TIMEOUT_MS)),
    });
  } catch (e) {
    return fail(502, e?.name === 'TimeoutError' ? 'BRIDGES timed out' : 'BRIDGES unreachable');
  }
  if (!r.ok) return fail(502, `BRIDGES said HTTP ${r.status}`); // body may echo the key: never forward it

  return { status: 200, json: { ok: true, url: `${base}/assignments/${assn}/${user}`, assignment: assn, sub } };
}
