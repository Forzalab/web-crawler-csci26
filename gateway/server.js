// server.js - plain node:http wrapper (Cloud Run, or local: `PORT=8080 npm start`).
//   POST /?assignment=N[&sub=K]   body = graph JSON from `wc --bridges`
//   POST|GET /api/pdf             24 h summary.pdf links (pdf.js), kept in memory here
import http from 'node:http';
import { handle } from './gateway.js';
import { handlePdf, memoryStore } from './pdf.js';

export function createServer(env = process.env, fetchImpl = fetch, store = memoryStore()) {
  const limit = Number(env.MAX_BYTES ?? 4 * 1024 * 1024);
  return http.createServer((req, res) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size <= limit + 1) chunks.push(c); // keep one byte over, so handle() sees "too big"
    });
    req.on('end', async () => {
      const url = new URL(req.url, 'http://x');
      if (url.pathname === '/api/pdf') {
        const p = await handlePdf({ method: req.method, query: Object.fromEntries(url.searchParams), headers: req.headers,
          body: Buffer.concat(chunks) }, env, store);
        res.writeHead(p.status, p.headers);
        return res.end(p.body);
      }
      const out = await handle(
        { method: req.method, query: Object.fromEntries(url.searchParams), headers: req.headers,
          body: Buffer.concat(chunks).toString('utf8') },
        env, fetchImpl);
      res.writeHead(out.status, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(out.json));
    });
  });
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const port = Number(process.env.PORT ?? 8080);
  createServer().listen(port, () => console.log(`gateway on :${port}`));
}
