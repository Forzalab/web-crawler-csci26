// api/pdf.js - Vercel function wrapper. Routes: POST /api/pdf?name=<host>-<mode> (raw PDF), GET /api/pdf?id=<id>
import { blobStore } from '../blob_store.js';
import { handlePdf } from '../pdf.js';

export default async function handler(req, res) {
  const chunks = [];
  let size = 0;
  for await (const c of req) {
    size += c.length;
    if (size <= 2 * 1024 * 1024 + 1) chunks.push(c); // one byte over, so handlePdf() sees "too big"
  }
  let out;
  try {
    out = await handlePdf({ method: req.method, query: req.query ?? {}, headers: req.headers, body: Buffer.concat(chunks) },
      process.env, blobStore());
  } catch (e) {
    console.error('pdf:', e?.name ?? 'error'); // never the token
    out = { status: 502, headers: { 'Content-Type': 'application/json' }, body: '{"ok":false,"error":"storage failed"}' };
  }
  for (const [k, v] of Object.entries(out.headers)) res.setHeader(k, v);
  res.status(out.status).send(out.body);
}
