import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import fs from 'node:fs';
import path from 'node:path';
import { createInterface } from 'node:readline';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import { familyKeys, keyToStr } from '../lsll/lib/cases.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const [cacheFile, serverFile = path.join(root, 'lsll/server.mjs')] = process.argv.slice(2);
assert(cacheFile, 'Usage: node tests/lsll_integration.mjs <scrambles.json> [server.mjs]');
const cache = JSON.parse(fs.readFileSync(cacheFile, 'utf8'));
const keys = familyKeys('kp').slice(0, 48).map(keyToStr);
assert(keys.every((key) => typeof cache[key]?.scramble === 'string'), 'Fixture needs the first K+ page');

fs.mkdirSync(path.join(root, 'temp'), { recursive: true });
const work = fs.mkdtempSync(path.join(root, 'temp/lsll-test-'));
fs.mkdirSync(path.join(work, 'cache'));
fs.writeFileSync(path.join(work, 'cache/scrambles.json'),
  JSON.stringify(Object.fromEntries(keys.map((key) => [key, cache[key]]))));
const service = spawn(process.execPath, [path.resolve(serverFile)], {
  env: { ...process.env, TEMP: work, TMP: work, LSLL_PORT: '0', LSLL_MANAGED: '1', LSLL_WORKSPACE: work },
  windowsHide: true,
});
let diagnostics = '';
service.stderr.on('data', (chunk) => { diagnostics += chunk; });
const closed = once(service, 'close');
const lines = createInterface({ input: service.stdout });
const timeout = setTimeout(() => service.kill(), 60000);

try {
  const [ready] = await once(lines, 'line', { signal: AbortSignal.timeout(5000) });
  const port = /^LSLL_READY (\d+)$/.exec(ready)?.[1];
  assert(port, ready);
  const base = `http://127.0.0.1:${port}`;
  async function request(route, body, code = 200) {
    const response = await fetch(base + route, body === undefined ? {} : {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body),
    });
    const data = await response.json();
    assert.equal(response.status, code, JSON.stringify(data));
    return data;
  }
  async function idle() {
    for (;;) {
      const state = await request('/api/status');
      if (state.phase === 'idle') return state;
      await delay(80);
    }
  }

  assert.match(await (await fetch(base)).text(), /LSLL · Cube Explorer/);
  const meta = await request('/api/meta');
  assert.equal(meta.groups.length, 41);
  assert.equal(meta.params.turns, 'URFDr');
  assert.equal(meta.ce.ok, true);
  await request('/api/scrape', { group: 'K+', page: 0 }, 400);
  await request('/api/scrape', { group: 'K+', page: 1 }, 202);
  let snapshot = await idle();
  assert.equal(snapshot.error, null);
  assert.deepEqual(snapshot.cases.map((item) => item.key), keys);
  assert.equal(snapshot.results.length, 0);
  assert(snapshot.cases.every((item) => item.scramble === cache[item.key].scramble));

  const single = '6rizr7p';
  assert(keys.includes(single));
  await request('/api/solve', {
    keys: [single], depth: 12, turns: 'URB', workers: 1, maxSols: 1, timeoutSec: 20,
  }, 202);
  snapshot = await idle();
  assert.equal(snapshot.error, null);
  const [result] = snapshot.results;
  assert.equal(result.key, single);
  assert.equal(result.count, 1, JSON.stringify(result));
  const solution = result.top[0];
  assert.equal(solution.verified, true);
  assert.equal(solution.alg, solution.moves.join(' '));
  assert.equal(solution.stm, solution.turns);
  assert.equal(solution.turns, solution.actions.reduce((sum, move) => sum + move.turn_cost, 0));
  assert.equal(result.stopReason, 'result_limit');
  assert.equal(result.searchComplete, false);
  assert(Number.isFinite(solution.mcc), snapshot.mccError || 'MCC score not produced');

  const output = await request('/api/export', {});
  const page = JSON.parse(fs.readFileSync(output.file, 'utf8'));
  assert.deepEqual(page.cases.find((item) => item.key === single).solutions, [solution]);
  await request('/api/scrape', { group: 'K+', page: 1 }, 202);
  assert.deepEqual((await idle()).results[0].top, [solution]);

  if (process.env.LSLL_TEST_IMAGES === '1') {
    const image = await fetch(base + '/img?size=96&alg=' + encodeURIComponent(cache[single].scramble));
    if (!image.ok) throw new Error(await image.text());
    assert.equal(image.headers.get('content-type'), 'image/png');
    assert.equal(Buffer.from(await image.arrayBuffer()).subarray(1, 4).toString(), 'PNG');
    const handout = await request('/api/handout', { topN: 1 });
    assert.equal(handout.imageError, null);
    const html = fs.readFileSync(handout.file, 'utf8');
    assert.match(html, /data:image\/png;base64,/);
    assert(html.includes(single));
  }

  await request('/api/solve', { depth: 22, turns: 'URFDr', workers: 1, maxSols: 0, timeoutSec: 30 }, 202);
  await request('/api/scrape', { group: 'K+', page: 2 }, 409);
  await request('/api/stop', {});
  snapshot = await idle();
  assert.equal(snapshot.error, null);
  assert(snapshot.results.some((item) => item.stopReason === 'cancelled'));
  assert.deepEqual(snapshot.results.find((item) => item.key === single).top.find((entry) => entry.alg === solution.alg), solution);

  await request('/api/reset', {});
  assert.equal((await request('/api/status')).results.length, 0);
  service.stdin.end();
  const [exitCode] = await closed;
  assert.equal(exitCode, 0, diagnostics);
  console.log(`PASS: LSLL cached list, verified Modern solve, MCC, export/reload, cancel, reset and shutdown. ${work}`);
} catch (error) {
  if (diagnostics) console.error(diagnostics);
  throw error;
} finally {
  clearTimeout(timeout);
  service.stdin.end();
  await Promise.race([closed, delay(3000)]);
  if (service.exitCode == null && service.signalCode == null) service.kill();
}
