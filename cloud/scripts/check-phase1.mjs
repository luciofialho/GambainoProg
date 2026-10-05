// Phase 1 check (Povoto/docs/cloud-plan.md, "Teste da Fase 1"): rows per day,
// gaps, and, for synthetic records, every value against the profile.
//   node scripts/check-phase1.mjs <povotoId> [batch] [--local]
// povotoId = site * 100 + PovotoNum. Without a batch, the latest one.
import { execSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { syntheticDay, syntheticPoint } from './synthetic.mjs';

const args = process.argv.slice(2);
const local = args.includes('--local');
const [povotoText, batchText] = args.filter(arg => arg !== '--local');
const povoto = Number(povotoText);
if (!Number.isInteger(povoto)) {
  console.error('Usage: node scripts/check-phase1.mjs <povotoId> [batch] [--local]');
  process.exit(1);
}

function query(sql) {
  const dir = mkdtempSync(join(tmpdir(), 'povoto-'));
  const file = join(dir, 'query.sql');
  writeFileSync(file, sql);
  try {
    const output = execSync(`npx wrangler d1 execute povoto ${local ? '--local' : '--remote'} --json --file "${file}"`,
      { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024, stdio: ['ignore', 'pipe', 'inherit'] });
    return JSON.parse(output)[0].results;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

const batch = batchText !== undefined ? Number(batchText)
  : query(`SELECT batch FROM batches WHERE povoto_id = ${povoto} ORDER BY last_epoch DESC LIMIT 1`)[0]?.batch;
if (batch === undefined) {
  console.error(`No batch for Povoto ${povoto}.`);
  process.exit(1);
}
const rows = query(`SELECT epoch, mode, temp, temp_sp, press, press_sp, sg, abv, rate, flags,
  synthetic_start, received_at FROM logs WHERE povoto_id = ${povoto} AND batch = ${batch} ORDER BY epoch`);
if (rows.length < 2) {
  console.log(`Povoto ${povoto}, batch ${batch}: ${rows.length} row(s), nothing to check.`);
  process.exit(0);
}

// Slot: the most frequent interval between rows.
const intervals = new Map();
for (let i = 1; i < rows.length; i++) {
  const step = rows[i].epoch - rows[i - 1].epoch;
  intervals.set(step, (intervals.get(step) ?? 0) + 1);
}
const slot = [...intervals.entries()].sort((a, b) => b[1] - a[1])[0][0];
const day = epoch => new Date(epoch * 1000).toISOString().slice(0, 10);

console.log(`Povoto ${povoto}, batch ${batch}: ${rows.length} rows, slot ${slot} s, ` +
  `${day(rows[0].epoch)} to ${new Date(rows.at(-1).epoch * 1000).toISOString().slice(0, 16)} (local)`);

console.log('\nRows per day (expected per full day):', Math.round(86400 / slot));
const perDay = new Map();
for (const row of rows) perDay.set(day(row.epoch), (perDay.get(day(row.epoch)) ?? 0) + 1);
for (const [date, count] of perDay) console.log(`  ${date}  ${count}`);

console.log('\nGaps (missing slots):');
let missing = 0;
for (let i = 1; i < rows.length; i++) {
  const step = rows[i].epoch - rows[i - 1].epoch;
  if (step > slot) {
    const lost = Math.round(step / slot) - 1;
    missing += lost;
    console.log(`  ${new Date(rows[i - 1].epoch * 1000).toISOString().slice(0, 16)} -> ` +
      `${new Date(rows[i].epoch * 1000).toISOString().slice(0, 16)}  ${lost} slot(s)`);
  }
}
if (!missing) console.log('  none');

// Upload delay: received (UTC) minus slot time (local epoch + offset).
const offset = Number(process.env.LOCAL_UTC_OFFSET_MINUTES ?? -180) * 60;
const delays = rows.map(row => row.received_at - (row.epoch - offset)).sort((a, b) => a - b);
const pct = p => delays[Math.min(delays.length - 1, Math.floor(p * delays.length))];
console.log(`\nDelay to the cloud (s): median ${pct(0.5)}, 95% ${pct(0.95)}, max ${delays.at(-1)}` +
  ' (backfilled rows after an outage raise the maximum)');

const synthetic = rows.filter(row => row.synthetic_start !== null);
if (synthetic.length) {
  const checks = [['sg', 'sg', 6e-5], ['abv', 'abv', 0.011], ['temp', 'temperature', 0.011],
    ['temp_sp', 'temperatureSetpoint', 0.011], ['press', 'pressure', 0.0011],
    ['press_sp', 'pressureSetpoint', 0.0011], ['rate', 'co2Rate', 0.0011]];
  let wrong = 0;
  for (const row of synthetic) {
    const expected = syntheticPoint(syntheticDay(row.epoch, row.synthetic_start));
    const bad = checks.filter(([column, field, tolerance]) => {
      const a = row[column];
      const b = expected[field];
      return (a === null) !== (b === null) || (a !== null && Math.abs(a - b) > tolerance);
    });
    if (row.flags !== expected.flags) bad.push(['flags']);
    if (bad.length && wrong++ < 10) {
      console.log(`  ${new Date(row.epoch * 1000).toISOString().slice(0, 16)}: ${bad.map(b => b[0]).join(', ')}`);
    }
  }
  console.log(`\nSynthetic values: ${synthetic.length - wrong} of ${synthetic.length} rows match the profile.`);
}
console.log(`\nSummary: ${missing} missing slot(s); duplicates are impossible (one row per Povoto and slot).`);
