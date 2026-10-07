// The only module that talks to the database: plain SQLite SQL over D1, so a
// move to another SQLite host replaces this file alone.
import type { AckRecord, LogRecord, SnapshotRecord, StateRecord } from './records';

export type Role = 'view' | 'edit';

export interface PovotoRow {
  id: number;
  site: number;
  num: number;
  last_epoch: number | null;
  mode: number | null;
  batch: number | null;
  batch_name: string | null;
}

export interface BatchRow {
  povoto_id: number;
  batch: number;
  name: string;
  date: string;
  og: number | null;
  first_epoch: number;
  last_epoch: number;
  // Latest batch state only (migration 0002); null until the first state line.
  state_epoch: number | null;
  chill_seconds: number | null;
  heat_seconds: number | null;
  mol_headspace: number | null;
  mol_dissolved: number | null;
  mol_ejected: number | null;
  expansions: number | null;
  dumped_volume: number | null;
}

export interface LogRow {
  povoto_id: number;
  epoch: number;
  batch: number;
  mode: number;
  temp: number | null;
  temp_sp: number | null;
  temp_slow: number | null;
  press: number | null;
  press_sp: number | null;
  press_slow: number | null;
  sg: number | null;
  abv: number | null;
  rate: number | null;
  flags: number;
  volume: number | null;
  co2_mass: number | null;
  reliefs_per_hour: number | null;
}

export interface ShareRow {
  token: string;
  povoto_id: number;
  batch: number;
  created_by: string;
  created_at: number;
}

export async function sidekickForTokenHash(db: D1Database, hash: string): Promise<{ site: number; name: string } | null> {
  return db.prepare('SELECT site, name FROM sidekicks WHERE token_hash = ?')
    .bind(hash).first<{ site: number; name: string }>();
}

// Stores the history records and batch states of one SideKick post. Returns
// how many history records were new.
export async function storeRecords(db: D1Database, site: number, records: LogRecord[],
                                   states: StateRecord[], receivedAt: number): Promise<number> {
  if (!records.length && !states.length) return 0;
  const statements: D1PreparedStatement[] = [];
  const povotos = new Set<number>(states.map(s => s.num));
  // Batch epoch range from the records of the post; name/date/OG from the
  // newest record that still carries them (firmware before the batch state).
  const batches = new Map<string, { id: number; first: number; last: LogRecord }>();
  const insertLog = db.prepare(
    `INSERT OR IGNORE INTO logs (povoto_id, epoch, batch, mode, temp, temp_sp, temp_slow,
       press, press_sp, press_slow, sg, abv, rate, flags, volume, co2_mass,
       reliefs_per_hour, synthetic_start, received_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);
  for (const r of records) {
    const id = site * 100 + r.num;
    povotos.add(r.num);
    statements.push(insertLog.bind(id, r.epoch, r.batch, r.mode, r.temp, r.tempSp, r.tempSlow,
      r.press, r.pressSp, r.pressSlow, r.sg, r.abv, r.rate, r.flags, r.volume, r.co2Mass,
      r.reliefsPerHour, r.syntheticStart, receivedAt));
    const key = `${id}:${r.batch}`;
    const batch = batches.get(key);
    if (!batch) batches.set(key, { id, first: r.epoch, last: r });
    else {
      batch.first = Math.min(batch.first, r.epoch);
      if (r.epoch >= batch.last.epoch) batch.last = r;
    }
  }
  const insertPovoto = db.prepare(
    'INSERT INTO povotos (id, site, num) VALUES (?, ?, ?) ON CONFLICT (id) DO NOTHING');
  for (const num of povotos) statements.push(insertPovoto.bind(site * 100 + num, site, num));
  // A late record (spool backlog) never overwrites newer batch details, and a
  // record without name (null: sent as batch state) leaves them alone.
  const upsertBatch = db.prepare(
    `INSERT INTO batches (povoto_id, batch, name, date, og, first_epoch, last_epoch)
     VALUES (?1, ?2, COALESCE(?3, ''), COALESCE(?4, ''), ?5, ?6, ?7)
     ON CONFLICT (povoto_id, batch) DO UPDATE SET
       name = CASE WHEN ?3 IS NOT NULL AND ?7 >= last_epoch THEN ?3 ELSE name END,
       date = CASE WHEN ?3 IS NOT NULL AND ?7 >= last_epoch THEN ?4 ELSE date END,
       og = CASE WHEN ?3 IS NOT NULL AND ?7 >= last_epoch THEN ?5 ELSE og END,
       first_epoch = MIN(first_epoch, ?6),
       last_epoch = MAX(last_epoch, ?7)`);
  for (const { id, first, last } of batches.values()) {
    statements.push(upsertBatch.bind(id, last.batch, last.batchName, last.batchDate, last.og,
      first, last.epoch));
  }
  // Batch state: one row per batch, overwritten only by a newer version.
  const upsertState = db.prepare(
    `INSERT INTO batches (povoto_id, batch, name, date, og, first_epoch, last_epoch, state_epoch,
       chill_seconds, heat_seconds, mol_headspace, mol_dissolved, mol_ejected, expansions,
       dumped_volume)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)
     ON CONFLICT (povoto_id, batch) DO UPDATE SET
       name = excluded.name, date = excluded.date, og = excluded.og,
       state_epoch = excluded.state_epoch,
       chill_seconds = excluded.chill_seconds, heat_seconds = excluded.heat_seconds,
       mol_headspace = excluded.mol_headspace, mol_dissolved = excluded.mol_dissolved,
       mol_ejected = excluded.mol_ejected, expansions = excluded.expansions,
       dumped_volume = excluded.dumped_volume
     WHERE excluded.state_epoch >= COALESCE(batches.state_epoch, 0)`);
  for (const s of states) {
    statements.push(upsertState.bind(site * 100 + s.num, s.batch, s.batchName, s.batchDate, s.og,
      s.epoch, s.chillSeconds, s.heatSeconds, s.molHeadspace, s.molDissolved, s.molEjected,
      s.expansions, s.dumpedVolume));
  }
  const results = await db.batch(statements);
  return results.slice(0, records.length).reduce((sum, result) => sum + (result.meta.changes ?? 0), 0);
}

// null ids = every Povoto.
export async function listPovotos(db: D1Database, ids: number[] | null): Promise<PovotoRow[]> {
  const filter = ids ? `WHERE p.id IN (${ids.map(() => '?').join(',')})` : '';
  const { results } = await db.prepare(
    `SELECT p.id, p.site, p.num, l.epoch AS last_epoch, l.mode, l.batch, b.name AS batch_name
     FROM povotos p
     LEFT JOIN logs l ON l.povoto_id = p.id
       AND l.epoch = (SELECT MAX(epoch) FROM logs WHERE povoto_id = p.id)
     LEFT JOIN batches b ON b.povoto_id = p.id AND b.batch = l.batch
     ${filter}
     ORDER BY p.site, p.num`).bind(...(ids ?? [])).all<PovotoRow>();
  return results;
}

export async function getPovoto(db: D1Database, id: number): Promise<{ id: number; site: number; num: number } | null> {
  return db.prepare('SELECT id, site, num FROM povotos WHERE id = ?').bind(id)
    .first<{ id: number; site: number; num: number }>();
}

export async function latestLog(db: D1Database, povotoId: number): Promise<LogRow | null> {
  return db.prepare('SELECT * FROM logs WHERE povoto_id = ? ORDER BY epoch DESC LIMIT 1')
    .bind(povotoId).first<LogRow>();
}

export async function listBatches(db: D1Database, povotoId: number): Promise<BatchRow[]> {
  const { results } = await db.prepare(
    'SELECT * FROM batches WHERE povoto_id = ? ORDER BY last_epoch DESC').bind(povotoId).all<BatchRow>();
  return results;
}

export async function getBatch(db: D1Database, povotoId: number, batch: number): Promise<BatchRow | null> {
  return db.prepare('SELECT * FROM batches WHERE povoto_id = ? AND batch = ?')
    .bind(povotoId, batch).first<BatchRow>();
}

export async function latestBatch(db: D1Database, povotoId: number): Promise<BatchRow | null> {
  return db.prepare('SELECT * FROM batches WHERE povoto_id = ? ORDER BY last_epoch DESC LIMIT 1')
    .bind(povotoId).first<BatchRow>();
}

// Fermenting rows only, as the device's own graph history.
export async function graphRows(db: D1Database, povotoId: number, batch: number): Promise<LogRow[]> {
  const { results } = await db.prepare(
    `SELECT epoch, sg, temp, temp_sp, press, press_sp, rate, abv, flags FROM logs
     WHERE povoto_id = ? AND batch = ? AND mode = 2 ORDER BY epoch`)
    .bind(povotoId, batch).all<LogRow>();
  return results;
}

export async function searchBatches(db: D1Database, query: string, ids: number[] | null): Promise<BatchRow[]> {
  const filters: string[] = [];
  const values: (string | number)[] = [];
  if (/^\d+$/.test(query)) {
    filters.push('batch = ?');
    values.push(Number(query));
  } else {
    filters.push("name LIKE ? ESCAPE '\\'");
    values.push(`%${query.replace(/[\\%_]/g, char => `\\${char}`)}%`);
  }
  if (ids) {
    filters.push(`povoto_id IN (${ids.map(() => '?').join(',')})`);
    values.push(...ids);
  }
  const { results } = await db.prepare(
    `SELECT * FROM batches WHERE ${filters.join(' AND ')} ORDER BY last_epoch DESC LIMIT 50`)
    .bind(...values).all<BatchRow>();
  return results;
}

export async function permissionsFor(db: D1Database, email: string): Promise<{ povoto_id: number; role: Role }[]> {
  const { results } = await db.prepare(
    'SELECT povoto_id, role FROM permissions WHERE email = ? COLLATE NOCASE')
    .bind(email).all<{ povoto_id: number; role: Role }>();
  return results;
}

export async function createShare(db: D1Database, share: ShareRow): Promise<void> {
  await db.prepare(
    'INSERT INTO shares (token, povoto_id, batch, created_by, created_at) VALUES (?, ?, ?, ?, ?)')
    .bind(share.token, share.povoto_id, share.batch, share.created_by, share.created_at).run();
}

export async function listShares(db: D1Database, povotoId: number): Promise<ShareRow[]> {
  const { results } = await db.prepare(
    `SELECT token, povoto_id, batch, created_by, created_at FROM shares
     WHERE povoto_id = ? AND revoked_at IS NULL ORDER BY created_at`).bind(povotoId).all<ShareRow>();
  return results;
}

export async function revokeShare(db: D1Database, povotoId: number, token: string, now: number): Promise<void> {
  await db.prepare('UPDATE shares SET revoked_at = ? WHERE token = ? AND povoto_id = ? AND revoked_at IS NULL')
    .bind(now, token, povotoId).run();
}

// Active link to an existing batch.
export async function getShare(db: D1Database, token: string): Promise<ShareRow | null> {
  return db.prepare(
    `SELECT s.token, s.povoto_id, s.batch, s.created_by, s.created_at FROM shares s
     JOIN batches b ON b.povoto_id = s.povoto_id AND b.batch = s.batch
     WHERE s.token = ? AND s.revoked_at IS NULL`).bind(token).first<ShareRow>();
}

// The batch, its log rows and its links.
export async function deleteBatch(db: D1Database, povotoId: number, batch: number): Promise<void> {
  await db.batch([
    db.prepare('DELETE FROM logs WHERE povoto_id = ? AND batch = ?').bind(povotoId, batch),
    db.prepare('DELETE FROM shares WHERE povoto_id = ? AND batch = ?').bind(povotoId, batch),
    db.prepare('DELETE FROM batches WHERE povoto_id = ? AND batch = ?').bind(povotoId, batch),
  ]);
}

// ------------------------------------------------------------------ phase 2
// Copy of the set points and rules (snapshots from the Povoto) and the
// requests made on the site (Povoto/docs/cloud-log.md).

// A request not given to the SideKick in this time expires; one given and not
// answered in this time is unconfirmed.
export const REQUEST_TIMEOUT_SECONDS = 180;

export type RequestKind = 'sp' | 'rule' | 'reset' | 'trigger' | 'snap';

export interface RequestRow {
  id: number;
  povoto_id: number;
  kind: RequestKind;
  base_hash: string | null;
  data: string;
  created_by: string;
  created_at: number;
  sent_at: number | null;
  status: string;
  message: string | null;
  done_at: number | null;
}

export interface SyncRow {
  povoto_id: number;
  setpoint_hash: string | null;
  rules_hash: string | null;
  edits_accepted: number | null;
  epoch: number;
}

export interface SnapshotRow {
  povoto_id: number;
  idx: number;
  hash: string;
  data: string;
  epoch: number;
}

// Snapshots (newest wins), answers and the hashes reported in batch states.
export async function storePhase2(db: D1Database, site: number, snapshots: SnapshotRecord[],
                                  acks: AckRecord[], states: StateRecord[], nowUtc: number): Promise<void> {
  const statements: D1PreparedStatement[] = [];
  const upsertSetpoints = db.prepare(
    `INSERT INTO setpoint_snapshots (povoto_id, hash, data, epoch) VALUES (?1, ?2, ?3, ?4)
     ON CONFLICT (povoto_id) DO UPDATE SET hash = excluded.hash, data = excluded.data, epoch = excluded.epoch
     WHERE excluded.epoch >= setpoint_snapshots.epoch`);
  const upsertRule = db.prepare(
    `INSERT INTO rule_snapshots (povoto_id, idx, hash, data, epoch) VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT (povoto_id, idx) DO UPDATE SET hash = excluded.hash, data = excluded.data, epoch = excluded.epoch
     WHERE excluded.epoch >= rule_snapshots.epoch`);
  for (const s of snapshots) {
    const id = site * 100 + s.num;
    statements.push(s.kind === 'sp' ? upsertSetpoints.bind(id, s.hash, s.data, s.epoch)
      : upsertRule.bind(id, s.index, s.hash, s.data, s.epoch));
  }
  // An answer after the timeout still counts: unconfirmed becomes applied/rejected.
  const answer = db.prepare(
    `UPDATE requests SET status = ?1, message = ?2, done_at = ?3
     WHERE id = ?4 AND povoto_id = ?5 AND status IN ('pending', 'sent', 'unconfirmed')`);
  for (const a of acks) {
    statements.push(answer.bind(a.ok ? 'applied' : 'rejected', a.message, nowUtc, a.id, site * 100 + a.num));
  }
  const upsertSync = db.prepare(
    `INSERT INTO povoto_sync (povoto_id, setpoint_hash, rules_hash, edits_accepted, epoch)
     VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT (povoto_id) DO UPDATE SET setpoint_hash = excluded.setpoint_hash,
       rules_hash = excluded.rules_hash, edits_accepted = excluded.edits_accepted, epoch = excluded.epoch
     WHERE excluded.epoch >= povoto_sync.epoch`);
  for (const s of states) {
    if (s.setpointHash === null && s.rulesHash === null) continue; // firmware without phase 2
    statements.push(upsertSync.bind(site * 100 + s.num, s.setpointHash, s.rulesHash,
      s.editsAccepted === null ? null : s.editsAccepted ? 1 : 0, s.epoch));
  }
  if (statements.length) await db.batch(statements);
}

export async function expireRequests(db: D1Database, nowUtc: number): Promise<void> {
  const limit = nowUtc - REQUEST_TIMEOUT_SECONDS;
  await db.batch([
    db.prepare(`UPDATE requests SET status = 'expired', done_at = ?1
                WHERE status = 'pending' AND created_at < ?2`).bind(nowUtc, limit),
    db.prepare(`UPDATE requests SET status = 'unconfirmed', done_at = ?1
                WHERE status = 'sent' AND kind != 'snap' AND sent_at < ?2`).bind(nowUtc, limit),
  ]);
}

// When the copy differs from what the Povoto reported, ask for its snapshots
// (one request per timeout at most).
export async function askStaleSnapshots(db: D1Database, povotoIds: number[], nowUtc: number): Promise<void> {
  if (!povotoIds.length) return;
  const { results } = await db.prepare(
    `SELECT s.povoto_id,
       s.setpoint_hash IS NOT NULL AND s.setpoint_hash IS NOT sp.hash AS setpoints_stale,
       s.rules_hash IS NOT NULL AND (SELECT COUNT(*) FROM rule_snapshots r
         WHERE r.povoto_id = s.povoto_id AND r.hash = s.rules_hash) < 8 AS rules_stale,
       EXISTS (SELECT 1 FROM requests q WHERE q.povoto_id = s.povoto_id AND q.kind = 'snap'
         AND q.created_at >= ?) AS asked
     FROM povoto_sync s LEFT JOIN setpoint_snapshots sp ON sp.povoto_id = s.povoto_id
     WHERE s.povoto_id IN (${povotoIds.map(() => '?').join(',')})`)
    .bind(nowUtc - REQUEST_TIMEOUT_SECONDS, ...povotoIds)
    .all<{ povoto_id: number; setpoints_stale: number; rules_stale: number; asked: number }>();
  const insert = db.prepare(
    `INSERT INTO requests (povoto_id, kind, data, created_by, created_at) VALUES (?, 'snap', '{}', 'cloud', ?)`);
  const statements = results.filter(r => (r.setpoints_stale || r.rules_stale) && !r.asked)
    .map(r => insert.bind(r.povoto_id, nowUtc));
  if (statements.length) await db.batch(statements);
}

// Pending requests of a site's Povotos, marked as given to its SideKick.
export async function takePendingRequests(db: D1Database, site: number, nowUtc: number): Promise<RequestRow[]> {
  const { results } = await db.prepare(
    `SELECT * FROM requests WHERE povoto_id BETWEEN ? AND ? AND status = 'pending' ORDER BY id LIMIT 10`)
    .bind(site * 100 + 1, site * 100 + 99).all<RequestRow>();
  if (results.length) {
    await db.prepare(`UPDATE requests SET status = 'sent', sent_at = ? WHERE id IN (${results.map(() => '?').join(',')})`)
      .bind(nowUtc, ...results.map(r => r.id)).run();
  }
  return results;
}

export async function createRequest(db: D1Database, request: {
  povoto_id: number; kind: RequestKind; base_hash: string; data: string; created_by: string; created_at: number;
}): Promise<void> {
  await db.prepare(
    `INSERT INTO requests (povoto_id, kind, base_hash, data, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?)`)
    .bind(request.povoto_id, request.kind, request.base_hash, request.data, request.created_by, request.created_at)
    .run();
}

// A user request still waiting for the Povoto (only one at a time).
export async function openRequest(db: D1Database, povotoId: number): Promise<RequestRow | null> {
  return db.prepare(
    `SELECT * FROM requests WHERE povoto_id = ? AND kind != 'snap' AND status IN ('pending', 'sent')
     ORDER BY id DESC LIMIT 1`).bind(povotoId).first<RequestRow>();
}

export async function recentRequests(db: D1Database, povotoId: number): Promise<RequestRow[]> {
  const { results } = await db.prepare(
    `SELECT * FROM requests WHERE povoto_id = ? AND kind != 'snap' ORDER BY id DESC LIMIT 6`)
    .bind(povotoId).all<RequestRow>();
  return results;
}

export async function getSync(db: D1Database, povotoId: number): Promise<SyncRow | null> {
  return db.prepare('SELECT * FROM povoto_sync WHERE povoto_id = ?').bind(povotoId).first<SyncRow>();
}

export async function getSetpointSnapshot(db: D1Database, povotoId: number): Promise<SnapshotRow | null> {
  return db.prepare('SELECT povoto_id, 0 AS idx, hash, data, epoch FROM setpoint_snapshots WHERE povoto_id = ?')
    .bind(povotoId).first<SnapshotRow>();
}

export async function getRuleSnapshots(db: D1Database, povotoId: number): Promise<SnapshotRow[]> {
  const { results } = await db.prepare('SELECT * FROM rule_snapshots WHERE povoto_id = ? ORDER BY idx')
    .bind(povotoId).all<SnapshotRow>();
  return results;
}
