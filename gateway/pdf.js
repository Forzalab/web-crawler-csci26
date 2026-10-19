// pdf.js - 24 h share links for `wc --report ... --pdf-link` (summary.pdf). Host-independent.
//   handlePdf({method, query, headers, body: Buffer}, env, store, now) -> {status, headers, body}
//   POST /api/pdf?name=<host>-<mode>   body = raw PDF  -> 200 {"ok":true,"url":"...?id=<id>","expires_at":"<ISO>"}
//   GET  /api/pdf?id=<id>                              -> the PDF inline, or 404 / 410 short text
//   sweep(store, now)                                  -> deletes stored PDFs past expiry (daily cron)
// id = <expiry unix seconds>-<32 hex> (128-bit random): the expiry needs no lookup, the random part is the secret.
// Stored at pdf/<id>/<name>.pdf. Wrapped by api/pdf.js + api/pdf-sweep.js (Vercel Blob) and server.js (memory).
import { randomBytes } from 'node:crypto';

export const TTL_S = 24 * 3600;
const MAX_BYTES = 2 * 1024 * 1024;
const ID = /^(\d{10})-([0-9a-f]{32})$/;
const GONE = 'This crawl summary expired after 24 h. Rerun with --offline --pdf-link.\n';

const json = (status, obj) => ({ status, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(obj) });
const text = (status, s) => ({ status, headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: s });
const expiryOf = (pathname) => Number(/^pdf\/(\d{10})-/.exec(pathname)?.[1] ?? 0);

export async function handlePdf(req, env = {}, store, now = Date.now()) {
  const sec = Math.floor(now / 1000);
  if (req.method === 'GET') {
    const m = ID.exec(String(req.query?.id ?? ''));
    if (!m) return text(404, 'No such crawl summary.\n');
    const blob = await store.find(`pdf/${m[0]}/`);
    if (!blob) return text(404, 'No such crawl summary.\n');
    const left = Number(m[1]) - sec;
    if (left <= 0) {
      await store.del([blob.url]);
      return text(410, GONE);
    }
    const name = blob.pathname.split('/').pop();
    return {
      status: 200,
      headers: {
        'Content-Type': 'application/pdf',
        'Content-Disposition': `inline; filename="${name}"`,
        'Cache-Control': `private, max-age=${left}`,
      },
      body: await store.read(blob.url),
    };
  }
  if (req.method !== 'POST') return json(405, { ok: false, error: 'GET or POST only' });

  if (env.GATEWAY_TOKEN && req.headers?.['x-gateway-token'] !== env.GATEWAY_TOKEN)
    return json(401, { ok: false, error: 'bad gateway token' });
  const body = Buffer.isBuffer(req.body) ? req.body : Buffer.alloc(0);
  if (body.length > MAX_BYTES) return json(413, { ok: false, error: `body over ${MAX_BYTES} bytes` });
  if (!body.subarray(0, 5).equals(Buffer.from('%PDF-'))) return json(400, { ok: false, error: 'body is not a PDF' });

  const name = String(req.query?.name ?? 'summary').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 80) || 'summary';
  const exp = sec + TTL_S;
  const id = `${exp}-${randomBytes(16).toString('hex')}`;
  await store.put(`pdf/${id}/${name}.pdf`, body);
  const base = env.PUBLIC_URL || `https://${req.headers?.['x-forwarded-host'] || req.headers?.host}`;
  return json(200, { ok: true, url: `${base.replace(/\/+$/, '')}/api/pdf?id=${id}`, expires_at: new Date(exp * 1000).toISOString() });
}

// Deletes every stored PDF past its expiry. Returns how many.
export async function sweep(store, now = Date.now()) {
  const sec = Math.floor(now / 1000);
  const old = (await store.list('pdf/')).filter((b) => expiryOf(b.pathname) <= sec).map((b) => b.url);
  if (old.length) await store.del(old);
  return old.length;
}

// In-memory store (tests, server.js). Same interface as blobStore() in api/pdf.js.
export function memoryStore() {
  const m = new Map(); // url -> {pathname, body}
  return {
    m,
    async put(pathname, body) { m.set(`mem:${pathname}`, { pathname, body }); },
    async find(prefix) {
      for (const [url, b] of m) if (b.pathname.startsWith(prefix)) return { url, pathname: b.pathname };
      return null;
    },
    async read(url) { return m.get(url).body; },
    async list(prefix) { return [...m].filter(([, b]) => b.pathname.startsWith(prefix)).map(([url, b]) => ({ url, pathname: b.pathname })); },
    async del(urls) { for (const u of urls) m.delete(u); },
  };
}
