// Posts synthetic records the way a SideKick does, to try the Worker without
// hardware:
//   node scripts/feed-test.mjs <ingest URL> <token> [povotoNum] [batch] [days]
// Sends `days` of 5-minute records ending now (default 2), Povoto 9, batch 9999.
// Delete the test batch afterwards on the Povoto page.
import { syntheticDay, syntheticPoint, SYNTHETIC_OG } from './synthetic.mjs';

const [url, token, numText = '9', batchText = '9999', daysText = '2'] = process.argv.slice(2);
if (!url || !token) {
  console.error('Usage: node scripts/feed-test.mjs <ingest URL> <token> [povotoNum] [batch] [days]');
  process.exit(1);
}
const SLOT = 300;
const LOCAL_OFFSET = -3 * 3600;
const now = Math.floor(Date.now() / 1000) + LOCAL_OFFSET;
const end = Math.floor(now / SLOT) * SLOT;
const start = end - Math.round(Number(daysText) * 86400 / SLOT) * SLOT;
const round = (value, decimals) => value === null ? null : Number(value.toFixed(decimals));

const lines = [];
for (let epoch = start; epoch <= end; epoch += SLOT) {
  const p = syntheticPoint(syntheticDay(epoch, start));
  lines.push(JSON.stringify({
    v: 1, p: Number(numText), e: epoch, m: 2, b: Number(batchText), bn: 'Teste sintético',
    bd: new Date(start * 1000).toISOString().slice(0, 10).split('-').reverse().join('/'),
    og: SYNTHETIC_OG, t: round(p.temperature, 2), ts: round(p.temperatureSetpoint, 2), tsl: null,
    pr: round(p.pressure, 3), ps: round(p.pressureSetpoint, 3), psl: null, sg: round(p.sg, 5),
    abv: round(p.abv, 2), r: round(p.co2Rate, 3), f: p.flags, vol: 40, co2: 120, rph: 2.5, ss: start,
  }));
}

// Same batch size as the SideKick (24 lines per post).
let inserted = 0;
for (let i = 0; i < lines.length; i += 24) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/x-ndjson' },
    body: lines.slice(i, i + 24).join('\n') + '\n',
  });
  // NDJSON answer: the first line is the summary (then requests, phase 2).
  const text = await response.text();
  let result = {};
  try { result = JSON.parse(text.split('\n')[0]); } catch { /* not JSON */ }
  if (!response.ok) {
    console.error(`HTTP ${response.status}`, result);
    process.exit(1);
  }
  inserted += result.inserted ?? 0;
}
console.log(`${lines.length} records sent, ${inserted} new.`);
