// api/pdf-sweep.js - daily Vercel cron (vercel.json): delete stored PDFs past their 24 h.
// With CRON_SECRET set, only Vercel's cron (Authorization: Bearer <CRON_SECRET>) may run it.
import { blobStore } from '../blob_store.js';
import { sweep } from '../pdf.js';

export default async function handler(req, res) {
  const secret = process.env.CRON_SECRET;
  if (secret && req.headers.authorization !== `Bearer ${secret}`) return res.status(401).json({ ok: false, error: 'cron only' });
  res.status(200).json({ ok: true, deleted: await sweep(blobStore()) });
}
