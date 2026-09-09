#!/usr/bin/env node
// Drive the WebAssembly build in a real browser and screenshot it -- the web
// counterpart of tools/run.sh.
//
//   tools/webtest.js [-o out.png] [-w seconds] [-k actions] [-u url] [--head]
//
// actions, comma separated (same vocabulary as run.sh):
//   d:S        wait S seconds
//   p:FILE     screenshot to FILE
//   k:KEY      tap a key (Puppeteer key name: ArrowUp, Space, F9, Enter, Escape)
//   k:KEY/down k:KEY/up   hold or release it
//   m:X/Y      click at canvas-relative X,Y
//   t:TEXT     type TEXT
//
// Needs a local Chrome and puppeteer-core, which tools/setup.sh installs into
// tools/node_modules (PUPPETEER_SKIP_DOWNLOAD=1: it drives the real Chrome).
const fs = require('fs');
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

const out = arg('-o', 'build/webshot.png');
const wait = parseFloat(arg('-w', '25'));
const keys = arg('-k', '');
const url = arg('-u', 'http://127.0.0.1:8080/pumpkin.html');
const head = process.argv.includes('--head');
const logFile = arg('-l', 'build/webtest.log');
const probe = arg('-e', '');
// A profile directory makes a run see what the one before it saved.
const profile = arg('--profile', '');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await puppeteer.launch({
    executablePath: CHROME,
    headless: head ? false : 'new',
    ...(profile ? { userDataDir: profile } : {}),
    dumpio: !!process.env.BOD_DUMPIO,
    args: [
      '--enable-unsafe-swiftshader',       // WebGL without a GPU
      '--autoplay-policy=no-user-gesture-required',
      '--window-size=900,900',
      // A headless or hidden tab throttles timers and rAF, which the emulator's
      // main loop is driven by.
      '--disable-background-timer-throttling',
      '--disable-renderer-backgrounding',
      '--disable-backgrounding-occluded-windows',
      '--mute-audio=false',
      ...(process.env.BOD_CHROME_LOG ? ['--enable-logging=stderr', '--v=1'] : []),
    ],
  });

  const page = await browser.newPage();
  await page.setViewport({ width: 900, height: 900 });

  const lines = [];
  // Written as they happen: a page that dies takes the end of the run with it.
  fs.writeFileSync(logFile, '');
  const note = (s) => {
    lines.push(s);
    try { fs.appendFileSync(logFile, s + '\n'); } catch (e) { /* keep going */ }
    if (process.env.BOD_VERBOSE) console.log(s);
  };
  page.on('console', (m) => note(`[${m.type()}] ${m.text()}`));
  page.on('pageerror', (e) => note(`[pageerror] ${e && (e.stack || e.message) || JSON.stringify(e)}`));
  page.on('error', (e) => note(`[crash] ${e && e.message}`));
  page.on('close', () => note('[page] closed'));
  page.on('framedetached', (f) => note(`[frame] detached ${f.url()}`));
  page.on('framenavigated', (f) => note(`[frame] navigated ${f.url()}`));
  browser.on('targetdestroyed', (t) => note(`[target] destroyed ${t.type()} ${t.url()}`));

  // The application runs on a pthread, which is a Worker of its own: an
  // exception there is reported on that target and nowhere else.
  let workerNo = 0;
  const watchWorker = async (t) => {
    if (!/worker|other/.test(t.type()) || !/pumpkin\.js/.test(t.url())) return;
    const n = ++workerNo;
    try {
      const s = await t.createCDPSession();
      await s.send('Runtime.enable');
      s.on('Runtime.exceptionThrown', (e) => {
        const d = e.exceptionDetails;
        note(`[worker${n}] EXCEPTION ${d.text} ${d.exception?.description || ''}`);
      });
      s.on('Runtime.consoleAPICalled', (e) => {
        if (e.type === 'error' || e.type === 'warning') {
          note(`[worker${n}] ${e.type}: ${e.args.map((a) => a.value ?? a.description).join(' ')}`);
        }
      });
    } catch (e) { note(`[worker${n}] attach failed: ${e.message}`); }
  };
  browser.on('targetcreated', watchWorker);
  browser.targets().forEach(watchWorker);
  page.on('requestfailed', (r) => note(`[failed] ${r.url()} ${r.failure().errorText}`));

  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 60000 });

  const isolated = await page.evaluate(() => window.crossOriginIsolated);
  note(`crossOriginIsolated=${isolated}`);

  // The screen has booted once the canvas stops being uniformly black.
  const canvas = await page.waitForSelector('#canvas', { timeout: 30000 });
  const bootedAt = Date.now();
  let booted = false;
  for (let t = 0; t < wait * 1000; t += 500) {
    await sleep(500);
    // The canvas may belong to a worker, so read it the way a person would:
    // a screenshot. A blank 320x320 frame is a few hundred bytes of PNG; one
    // with a picture in it is several thousand.
    const n = await canvas.screenshot({ encoding: 'binary' })
      .then((b) => b.length)
      .catch((e) => { note(`[poll] ${e.message}`); return -1; });
    if (n > 2000) { booted = true; note(`booted after ${Date.now() - bootedAt}ms (${n} bytes of frame)`); break; }
  }
  if (!booted) note(`screen still blank after ${wait}s`);

  if (probe) {
    const r = await page.evaluate(probe).catch((e) => 'probe failed: ' + e.message);
    console.log('probe: ' + (typeof r === 'string' ? r : JSON.stringify(r)));
  }

  const shot = async (file) => {
    await canvas.screenshot({ path: file });
    note(`captured ${file}`);
  };

  for (const action of keys ? keys.split(',') : []) {
    const [op, val] = [action.slice(0, 1), action.slice(2)];
    if (op === 'd') await sleep(parseFloat(val) * 1000);
    else if (op === 'p') await shot(val);
    else if (op === 't') await page.keyboard.type(val);
    else if (op === 'm') {
      const [x, y] = val.split('/').map(Number);
      const box = await canvas.boundingBox();
      await page.mouse.click(box.x + x * (box.width / 320), box.y + y * (box.height / 320));
    } else if (op === 'k') {
      const [name, state] = val.split('/');
      if (state === 'down') await page.keyboard.down(name);
      else if (state === 'up') await page.keyboard.up(name);
      else { await page.keyboard.down(name); await sleep(60); await page.keyboard.up(name); }
    }
  }

  await sleep(400);
  await shot(out);
  console.log(`${booted ? 'booted' : 'BLANK'}  shot=${out}  log=${logFile}  isolated=${isolated}`);
  const bad = lines.filter((l) => /pageerror|abort|RuntimeError|failed/i.test(l));
  if (bad.length) console.log('problems:\n' + bad.slice(0, 20).join('\n'));
  await browser.close();
  process.exit(booted ? 0 : 1);
})();
