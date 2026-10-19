// blob_store.js - pdf.js store on Vercel Blob (BLOB_READ_WRITE_TOKEN from the connected store).
import { del, list, put } from '@vercel/blob';

export function blobStore() {
  return {
    async put(pathname, body) {
      await put(pathname, body, { access: 'public', addRandomSuffix: false, contentType: 'application/pdf' });
    },
    async find(prefix) {
      const { blobs } = await list({ prefix, limit: 1 });
      return blobs[0] ? { url: blobs[0].url, pathname: blobs[0].pathname } : null;
    },
    async read(url) {
      const r = await fetch(url, { cache: 'no-store' });
      if (!r.ok) throw new Error(`blob read HTTP ${r.status}`);
      return Buffer.from(await r.arrayBuffer());
    },
    async list(prefix) {
      const out = [];
      let cursor;
      do {
        const page = await list({ prefix, cursor, limit: 1000 });
        out.push(...page.blobs.map((b) => ({ url: b.url, pathname: b.pathname })));
        cursor = page.hasMore ? page.cursor : undefined;
      } while (cursor);
      return out;
    },
    async del(urls) { await del(urls); },
  };
}
