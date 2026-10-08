// Compatibility of the ingest with every firmware generation still in use
// (Povoto/docs/cloud-plan.md, "Ambientes e versões"). Each file in fixtures/
// holds lines exactly as one generation sends them; a new generation adds a
// file, and no file is removed while a Povoto may still run that firmware.
//   npm test
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { parseBody } from '../src/records.ts';

const NOW = 1790100000; // after every fixture epoch
const dir = join(import.meta.dirname, 'fixtures');
const fixture = name => readFileSync(join(dir, name), 'utf8');

test('every fixture line is accepted', () => {
  for (const name of readdirSync(dir).filter(n => n.endsWith('.ndjson'))) {
    const body = fixture(name);
    const lines = body.split('\n').filter(l => l.trim()).length;
    const parsed = parseBody(body, NOW);
    assert.equal(parsed.rejected, 0, `${name}: rejected lines`);
    const accepted = parsed.records.length + parsed.states.length + parsed.snapshots.length + parsed.acks.length;
    assert.equal(accepted, lines, `${name}: every line counted once`);
  }
});

test('phase 1 record carrying the batch name (before state lines)', () => {
  const { records } = parseBody(fixture('2026-09-phase1-record-with-batch.ndjson'), NOW);
  assert.equal(records[0].batchName, 'Pilsen');
  assert.equal(records[0].og, 1.048);
  assert.equal(records[0].syntheticStart, null);
});

test('phase 1 state line: no phase 2 fields means unknown, not off', () => {
  const { records, states } = parseBody(fixture('2026-10-phase1-record-and-state.ndjson'), NOW);
  assert.equal(records[0].batchName, null);  // absent: keep the stored one
  assert.equal(records[0].rate, null);
  assert.equal(states[0].setpointHash, null);
  assert.equal(states[0].editsAccepted, null);
});

test('phase 2 lines', () => {
  const { records, states, snapshots, acks } = parseBody(fixture('2026-10-phase2.ndjson'), NOW);
  assert.equal(records[0].syntheticStart, 1789990000);
  assert.equal(states[0].setpointHash, '51f4d0c8');
  assert.equal(states[0].editsAccepted, true);
  assert.deepEqual(snapshots.map(s => [s.kind, s.index]), [['sp', 0], ['r', 0]]);
  assert.deepEqual(acks[0], { num: 3, id: 42, ok: true, message: '' });
});

test('a newer firmware: unknown fields are ignored, unknown kinds rejected alone', () => {
  const lines = fixture('2026-10-phase2.ndjson').trim().split('\n');
  const newer = [
    lines[0].replace(/}$/, ',"zz":5}'),          // new field in a record
    lines[1].replace(/}$/, ',"pv":3,"new":"x"}'), // new fields in a state line
    '{"v":1,"k":"future","p":3,"e":1790000600}',  // a kind this cloud does not know
  ].join('\n');
  const parsed = parseBody(newer, NOW);
  assert.equal(parsed.records.length, 1);
  assert.equal(parsed.states.length, 1);
  assert.equal(parsed.rejected, 1);
});
