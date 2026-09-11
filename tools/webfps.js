#!/usr/bin/env node
// Measure how many frames a second the WebAssembly build actually puts on the
// canvas, while it is being ridden.
//
//   tools/webfps.js [-u url] [-s seconds] [-w warmup] [--head] [-o shot.png]
//
// Frames are counted where they land rather than where they are produced: the
// page's 2D context is wrapped before anything loads, so both present paths --
// SDL's putImageData from the worker and the page's own -- are counted the same
// way, and neither build has to be told it is being measured.
//
// It boots the game, taps into the first level, holds the pedal down and reports
// the rate over the sample window, plus the spread of the gaps between frames,
// which is what "smooth" means: sixty frames a second arriving in bursts of two
// looks worse than fifty evenly spaced.
const path = require('path');

const puppeteer = (() => {
  for (const m of ['puppeteer-core', path.join(__dirname, 'node_modules', 'puppeteer-core')]) {
    try { return require(m); } catch (e) { /* try the next one */ }
  }
  console.error('puppeteer-core not found. Run: PUPPETEER_SKIP_DOWNLOAD=1 npm --prefix tools install');
  process.exit(2);
})();

const CHROME = process.env.BOD_CHROME ||
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

function arg(flag, dflt) {
  const i = process.argv.indexOf(flag);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : dflt;
}

const url = arg('-u', 'http://127.0.0.1:8099/pumpkin.html');
const sample = parseFloat(arg('-s', '6'));
const warmup = parseFloat(arg('-w', '2'));
const head = process.argv.includes('--head');
const shot = arg('-o', '');
const verbose = process.argv.includes('-v');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await puppeteer.launch({
    executablePath: CHROME,
    headless: head ? false : 'new',
    args: [
      '--enable-unsafe-swiftshader',
      '--autoplay-policy=no-user-gesture-required',
      '--window-size=900,900',
      '--disable-background-timer-throttling',
      '--disable-renderer-backgrounding',
      '--disable-backgrounding-occluded-windows',
    ],
  });

  const page = await browser.newPage();
  await page.setViewport({ width: 900, height: 900 });
  if (verbose) {
    page.on('console', (m) => console.log(`[${m.type()}] ${m.text()}`));
    page.on('pageerror', (e) => console.log(`[pageerror] ${e.message}`));
  }

  // Count every frame that reaches a 2D context, whoever draws it, and keep
  // the arrival times so the gaps between them can be looked at.
  await page.evaluateOnNewDocument(() => {
    window.__bodFrames = 0;
    window.__bodTimes = [];
    const put = CanvasRenderingContext2D.prototype.putImageData;
    CanvasRenderingContext2D.prototype.putImageData = function (...a) {
      window.__bodFrames++;
      window.__bodTimes.push(performance.now());
      if (window.__bodTimes.length > 4000) window.__bodTimes.shift();
      return put.apply(this, a);
    };
    // Some present paths hand the browser a bitmap instead; count those too.
    const draw = CanvasRenderingContext2D.prototype.drawImage;
    CanvasRenderingContext2D.prototype.drawImage = function (...a) {
      window.__bodFrames++;
      window.__bodTimes.push(performance.now());
      if (window.__bodTimes.length > 4000) window.__bodTimes.shift();
      return draw.apply(this, a);
    };
  });

  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });
  const canvas = await page.waitForSelector('#canvas', { timeout: 30000 });

  // Booted once the screen stops being one flat colour.
  let booted = false;
  for (let t = 0; t < 30000; t += 500) {
    await sleep(500);
    const n = await canvas.screenshot({ encoding: 'binary' }).then((b) => b.length).catch(() => -1);
    if (n > 2000) { booted = true; break; }
  }
  if (!booted) { console.log('BLANK: the game never came up'); await browser.close(); process.exit(1); }

  // Past the intro and into the first level: two taps, the way ride.txt does it.
  const box = await canvas.boundingBox();
  const tap = async () => page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await sleep(1500); await tap();
  await sleep(3000); await tap();
  await sleep(5000);

  // Pedal in bursts rather than flat out, and tap the brake between them. Held
  // down the whole way the bike clears the first jump and lands on its head,
  // and what is being measured stops being a ride; the brake is also what
  // "try again" is bound to, so a ride that ends in the grass starts again by
  // itself and the sample is spent riding either way.
  let riding = true;
  const ride = (async () => {
    for (let n = 0; riding; n++) {
      await page.keyboard.down('ArrowUp').catch(() => {});
      await sleep(220);
      await page.keyboard.up('ArrowUp').catch(() => {});
      if (!riding) break;
      await sleep(180);
      if (n % 4 === 3) {
        await page.keyboard.down('ArrowDown').catch(() => {});
        await sleep(90);
        await page.keyboard.up('ArrowDown').catch(() => {});
      }
      await sleep(200);
    }
  })();

  await sleep(warmup * 1000);
  const snap = () => page.evaluate(() => ({
    n: window.__bodFrames, t: performance.now(),
    stats: (window.Module && Module.bodPresentStats && Module.bodPresentStats()) || null,
  }));
  const before = await snap();
  await sleep(sample * 1000);
  const after = await page.evaluate(() => ({
    n: window.__bodFrames, t: performance.now(), times: window.__bodTimes.slice(),
    stats: (window.Module && Module.bodPresentStats && Module.bodPresentStats()) || null,
  }));
  riding = false;
  await ride;

  const frames = after.n - before.n;
  const secs = (after.t - before.t) / 1000;
  const fps = frames / secs;

  // The gaps between the frames that landed during the sample.
  const times = after.times.filter((t) => t >= before.t);
  const gaps = [];
  for (let i = 1; i < times.length; i++) gaps.push(times[i] - times[i - 1]);
  gaps.sort((a, b) => a - b);
  const pct = (p) => (gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(gaps.length * p))] : 0);

  console.log(`${fps.toFixed(1)} fps   (${frames} frames in ${secs.toFixed(2)}s)`);
  if (gaps.length) {
    console.log(`gap between frames: median ${pct(0.5).toFixed(1)}ms  p90 ${pct(0.9).toFixed(1)}ms  worst ${gaps[gaps.length - 1].toFixed(1)}ms`);
    const late = gaps.filter((g) => g > 25).length;
    console.log(`gaps over 25ms: ${late} of ${gaps.length}`);
    // Where the long ones fall, which is how a stall tells itself apart from a
    // rate: a level load happens once, a saver every five seconds.
    const stalls = [];
    for (let i = 1; i < times.length; i++) {
      const g = times[i] - times[i - 1];
      if (g > 100) stalls.push(`${((times[i - 1] - before.t) / 1000).toFixed(1)}s:+${g.toFixed(0)}ms`);
    }
    if (stalls.length) console.log(`stalls: ${stalls.join(' ')}`);
  }

  if (before.stats && after.stats) {
    const d = (k) => after.stats[k] - before.stats[k];
    console.log(`page: ${(d('refreshes') / secs).toFixed(1)} refreshes/s, ` +
                `${(d('published') / secs).toFixed(1)} published/s, ` +
                `${(d('presented') / secs).toFixed(1)} presented/s, ` +
                `${d('stale')} refreshes found nothing new`);
  }

  if (shot) { await canvas.screenshot({ path: shot }); console.log(`shot=${shot}`); }
  await browser.close();
})();
