// npm test   (no network: fake fetch + a fake BRIDGES on 127.0.0.1)
import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { handle } from '../gateway.js';
import { createServer } from '../server.js';

const ENV = { BRIDGES_USER: 'u ser', BRIDGES_KEY: 'SECRET-KEY', BRIDGES_SERVER: 'http://bridges.test/' };
const GRAPH = JSON.stringify({ visual: 'GraphAdjacencyList', title: 't', nodes: [{ name: 'a' }], links: [] });
const req = (over = {}) => ({ method: 'POST', query: { assignment: '26' }, headers: {}, body: GRAPH, ...over });

function fakeFetch(status = 200) {
  const calls = [];
  const f = async (url, opts) => { calls.push({ url, opts }); return { ok: status < 300, status }; };
  f.calls = calls;
  return f;
}

test('happy path: forwards body, builds BRIDGES URL, hides key', async () => {
  const f = fakeFetch();
  const out = await handle(req({ query: { assignment: '26', sub: '3' } }), ENV, f);
  assert.equal(out.status, 200);
  assert.deepEqual(out.json, { ok: true, url: 'http://bridges.test/assignments/26/u%20ser', assignment: 26, sub: 3 });
  assert.equal(f.calls.length, 1);
  assert.equal(f.calls[0].url, 'http://bridges.test/assignments/26.03?apikey=SECRET-KEY&username=u%20ser');
  assert.equal(f.calls[0].opts.body, GRAPH);
  assert.equal(f.calls[0].opts.headers['Content-Type'], 'text/plain');
  assert.ok(!JSON.stringify(out).includes('SECRET'));
});

test('sub defaults to 0 -> .00', async () => {
  const f = fakeFetch();
  await handle(req(), ENV, f);
  assert.match(f.calls[0].url, /\/assignments\/26\.00\?/);
});

test('big assignment ids pass (8 digits, e.g. 69696969)', async () => {
  const f = fakeFetch();
  const out = await handle(req({ query: { assignment: '69696969' } }), ENV, f);
  assert.equal(out.status, 200);
  assert.equal(out.json.assignment, 69696969);
  assert.match(f.calls[0].url, /\/assignments\/69696969\.00\?/);
});

const rejects = [
  ['GET', req({ method: 'GET' }), ENV, 405],
  ['no creds', req(), {}, 500],
  ['no assignment', req({ query: {} }), ENV, 400],
  ['assignment 0', req({ query: { assignment: '0' } }), ENV, 400],
  ['assignment over 8 digits', req({ query: { assignment: '100000000' } }), ENV, 400],
  ['assignment not int', req({ query: { assignment: '2.5' } }), ENV, 400],
  ['assignment over custom max', req({ query: { assignment: '27' } }), { ...ENV, ASSIGNMENT_MIN: '26', ASSIGNMENT_MAX: '26' }, 400],
  ['sub 99', req({ query: { assignment: '26', sub: '99' } }), ENV, 400],
  ['not JSON', req({ body: '{nope' }), ENV, 400],
  ['wrong visual', req({ body: JSON.stringify({ visual: 'Array', nodes: [], links: [] }) }), ENV, 400],
  ['too big', req(), { ...ENV, MAX_BYTES: '10' }, 413],
  ['too many nodes', req(), { ...ENV, MAX_NODES: '0' }, 413],
  ['token missing', req(), { ...ENV, GATEWAY_TOKEN: 't0k' }, 401],
];
for (const [name, r, env, status] of rejects) {
  test(`reject: ${name} -> ${status}, no upstream call`, async () => {
    const f = fakeFetch();
    const out = await handle(r, env, f);
    assert.equal(out.status, status);
    assert.equal(out.json.ok, false);
    assert.equal(f.calls.length, 0);
    assert.ok(!JSON.stringify(out).includes('SECRET'));
  });
}

test('token ok -> forwards', async () => {
  const f = fakeFetch();
  const out = await handle(req({ headers: { 'x-gateway-token': 't0k' } }), { ...ENV, GATEWAY_TOKEN: 't0k' }, f);
  assert.equal(out.status, 200);
});

test('BRIDGES 401 -> 502, upstream body never forwarded', async () => {
  const out = await handle(req(), ENV, fakeFetch(401));
  assert.deepEqual(out, { status: 502, json: { ok: false, error: 'BRIDGES said HTTP 401' } });
});

test('BRIDGES unreachable / timeout -> 502', async () => {
  const boom = async () => { throw new TypeError('fetch failed SECRET-KEY'); };
  assert.deepEqual((await handle(req(), ENV, boom)).json.error, 'BRIDGES unreachable');
  const slow = async () => { const e = new Error('t'); e.name = 'TimeoutError'; throw e; };
  assert.deepEqual((await handle(req(), ENV, slow)).json.error, 'BRIDGES timed out');
});

test('end to end over HTTP: wc -> gateway -> fake BRIDGES (all on 127.0.0.1)', async () => {
  const seen = [];
  const bridges = http.createServer((q, s) => {
    let b = ''; q.on('data', (c) => (b += c)); q.on('end', () => { seen.push({ url: q.url, b }); s.end('ok'); });
  });
  await new Promise((r) => bridges.listen(0, '127.0.0.1', r));
  const env = { ...ENV, BRIDGES_SERVER: `http://127.0.0.1:${bridges.address().port}` };
  const gw = createServer(env);
  await new Promise((r) => gw.listen(0, '127.0.0.1', r));
  try {
    const res = await fetch(`http://127.0.0.1:${gw.address().port}/?assignment=26&sub=1`, { method: 'POST', body: GRAPH });
    const json = await res.json();
    assert.equal(res.status, 200);
    assert.equal(json.url, `${env.BRIDGES_SERVER}/assignments/26/u%20ser`);
    assert.equal(seen.length, 1);
    assert.equal(seen[0].url, '/assignments/26.01?apikey=SECRET-KEY&username=u%20ser');
    assert.equal(seen[0].b, GRAPH);
  } finally {
    gw.close(); bridges.close();
  }
});
