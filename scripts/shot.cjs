// shot.cjs - headless Chromium screenshot as JPEG (Playwright).
//   node scripts/shot.cjs page URL OUT.jpg [wait_ms]   whole 1200x900 viewport, after wait_ms (BRIDGES draws with JS)
//   node scripts/shot.cjs file PAGE.html OUT.jpg       just the <pre id="t"> element (scripts/ansi_html.py output)
// Playwright from the project or global modules (NODE_PATH=$(npm root -g)). Browser: PLAYWRIGHT_BROWSERS_PATH as usual.
const { chromium } = require('playwright');
const path = require('node:path');

(async () => {
  const [kind, target, out, wait] = process.argv.slice(2);
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({ viewport: { width: 1200, height: 900 }, deviceScaleFactor: 2 });
    if (kind === 'file') {
      await page.goto('file://' + path.resolve(target));
      await page.locator('#t').screenshot({ path: out, type: 'jpeg', quality: 85 });
    } else {
      await page.goto(target, { waitUntil: 'load', timeout: 30000 });
      await page.waitForTimeout(Number(wait ?? 8000));
      await page.screenshot({ path: out, type: 'jpeg', quality: 85 });
    }
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error('shot:', e.message); process.exit(1); });
