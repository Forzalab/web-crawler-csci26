// api/bridges.js - Vercel function wrapper. Route: POST /api/bridges?assignment=N[&sub=K]
import { handle } from '../gateway.js';

export default async function handler(req, res) {
  const chunks = [];
  for await (const c of req) chunks.push(c);
  const out = await handle(
    { method: req.method, query: req.query ?? {}, headers: req.headers,
      body: Buffer.concat(chunks).toString('utf8') },
    process.env);
  res.status(out.status).json(out.json);
}
