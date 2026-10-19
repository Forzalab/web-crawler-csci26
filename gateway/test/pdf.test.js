// npm test   (no network: in-memory store instead of Vercel Blob)
import test from 'node:test';
import assert from 'node:assert/strict';
import { handlePdf, memoryStore, sweep, TTL_S } from '../pdf.js';
import { createServer } from '../server.js';

const PDF = Buffer.from('%PDF-1.4\nhello\n%%EOF\n');
const NOW = Date.UTC(2026, 9, 19, 2, 0, 0);
const HEAD = { host: 'gw.test' };
const post = (store, over = {}, env = {}) =>
  handlePdf({ method: 'POST', query: { name: 'www.iana.org-bfs' }, headers: HEAD, body: PDF, ...over }, env, store, NOW);
const get = (store, id, now = NOW) => handlePdf({ method: 'GET', query: { id }, headers: HEAD }, {}, store, now);
const idOf = (out) => new URL(JSON.parse(out.body).url).searchParams.get('id');

test('POST stores the PDF, returns a 24 h link with a 128-bit id', async () => {
  const s = memoryStore();
  const out = await post(s);
  assert.equal(out.status, 200);
  const j = JSON.parse(out.body);
  assert.equal(j.ok, true);
  assert.match(j.url, /^https:\/\/gw\.test\/api\/pdf\?id=\d{10}-[0-9a-f]{32}$/);
  assert.equal(j.expires_at, new Date(NOW + TTL_S * 1000).toISOString());
  assert.equal(s.m.size, 1);
  assert.match([...s.m.values()][0].pathname, /^pdf\/\d{10}-[0-9a-f]{32}\/www\.iana\.org-bfs\.pdf$/);
  assert.notEqual(idOf(await post(s)), idOf(out));
});

test('GET before expiry streams it inline', async () => {
  const s = memoryStore();
  const id = idOf(await post(s));
  const out = await get(s, id, NOW + 3600 * 1000);
  assert.equal(out.status, 200);
  assert.equal(out.headers['Content-Type'], 'application/pdf');
  assert.equal(out.headers['Content-Disposition'], 'inline; filename="www.iana.org-bfs.pdf"');
  assert.equal(out.headers['Cache-Control'], `private, max-age=${TTL_S - 3600}`);
  assert.deepEqual(out.body, PDF);
});

test('GET after 24 h -> 410 and deleted', async () => {
  const s = memoryStore();
  const id = idOf(await post(s));
  const out = await get(s, id, NOW + TTL_S * 1000);
  assert.equal(out.status, 410);
  assert.match(out.body, /expired after 24 h\. Rerun with --offline --pdf-link/);
  assert.equal(s.m.size, 0);
});

test('GET unknown / malformed id -> 404', async () => {
  const s = memoryStore();
  await post(s);
  assert.equal((await get(s, '1792461600-' + '0'.repeat(32))).status, 404);
  assert.equal((await get(s, '../etc')).status, 404);
  assert.equal((await get(s, undefined)).status, 404);
});

const rejects = [
  ['bad token', { headers: { ...HEAD, 'x-gateway-token': 'nope' } }, { GATEWAY_TOKEN: 't0k' }, 401],
  ['no token', {}, { GATEWAY_TOKEN: 't0k' }, 401],
  ['not a PDF', { body: Buffer.from('<html>') }, {}, 400],
  ['empty', { body: undefined }, {}, 400],
  ['oversize', { body: Buffer.concat([PDF, Buffer.alloc(2 * 1024 * 1024)]) }, {}, 413],
  ['PUT', { method: 'PUT' }, {}, 405],
];
for (const [name, over, env, status] of rejects) {
  test(`reject: ${name} -> ${status}, nothing stored, token never echoed`, async () => {
    const s = memoryStore();
    const out = await post(s, over, env);
    assert.equal(out.status, status);
    assert.equal(s.m.size, 0);
    assert.ok(!out.body.includes('t0k'));
  });
}

test('token ok -> stored; name is sanitized', async () => {
  const s = memoryStore();
  const out = await post(s, { headers: { ...HEAD, 'x-gateway-token': 't0k' }, query: { name: '../a b"c' } }, { GATEWAY_TOKEN: 't0k' });
  assert.equal(out.status, 200);
  assert.match([...s.m.values()][0].pathname, /\/\.\._a_b_c\.pdf$/);
});

test('sweep deletes only expired PDFs', async () => {
  const s = memoryStore();
  await post(s);
  await handlePdf({ method: 'POST', query: {}, headers: HEAD, body: PDF }, {}, s, NOW + 3600 * 1000);
  assert.equal(await sweep(s, NOW + TTL_S * 1000), 1);
  assert.equal(s.m.size, 1);
});

test('end to end over HTTP: POST then GET on server.js', async () => {
  const gw = createServer({});
  await new Promise((r) => gw.listen(0, '127.0.0.1', r));
  try {
    const base = `http://127.0.0.1:${gw.address().port}`;
    const res = await fetch(`${base}/api/pdf?name=x-dfs`, { method: 'POST', body: PDF });
    const j = await res.json();
    assert.equal(res.status, 200);
    const id = new URL(j.url).searchParams.get('id');
    const pdf = await fetch(`${base}/api/pdf?id=${id}`);
    assert.equal(pdf.status, 200);
    assert.equal(pdf.headers.get('content-type'), 'application/pdf');
    assert.deepEqual(Buffer.from(await pdf.arrayBuffer()), PDF);
  } finally {
    gw.close();
  }
});
