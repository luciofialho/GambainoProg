// Cloud log records as sent by the Povoto (Povoto/docs/cloud-log.md), one JSON
// object per line, forwarded unchanged by the SideKick.

export interface LogRecord {
  num: number;        // PovotoNum
  epoch: number;      // local epoch, start of the slot
  mode: number;
  batch: number;
  batchName: string;
  batchDate: string;
  og: number | null;
  temp: number | null;
  tempSp: number | null;
  tempSlow: number | null;
  press: number | null;
  pressSp: number | null;
  pressSlow: number | null;
  sg: number | null;
  abv: number | null;
  rate: number | null;
  flags: number;
  volume: number | null;
  co2Mass: number | null;
  reliefsPerHour: number | null;
  syntheticStart: number | null;
}

const MIN_EPOCH = 1577836800; // 2020-01-01

function int(value: unknown, min: number, max: number): number | undefined {
  return typeof value === 'number' && Number.isInteger(value) && value >= min && value <= max
    ? value : undefined;
}

// A missing or null value is null; anything else must be a finite number.
function num(value: unknown): number | null | undefined {
  if (value === undefined || value === null) return null;
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

function text(value: unknown, maxLength: number): string | undefined {
  if (value === undefined || value === null) return '';
  return typeof value === 'string' ? value.slice(0, maxLength) : undefined;
}

// Returns null when the line is not a valid record.
export function parseRecord(line: string, nowUtc: number): LogRecord | null {
  let raw: Record<string, unknown>;
  try {
    const parsed: unknown = JSON.parse(line);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return null;
    raw = parsed as Record<string, unknown>;
  } catch {
    return null;
  }
  if (raw.v !== 1) return null;
  const record = {
    num: int(raw.p, 1, 99),
    // Local epoch is behind UTC here; a day of margin covers any offset.
    epoch: int(raw.e, MIN_EPOCH, nowUtc + 86400),
    mode: int(raw.m, 0, 3),
    batch: int(raw.b, 1, 65535),
    batchName: text(raw.bn, 64),
    batchDate: text(raw.bd, 16),
    og: num(raw.og),
    temp: num(raw.t),
    tempSp: num(raw.ts),
    tempSlow: num(raw.tsl),
    press: num(raw.pr),
    pressSp: num(raw.ps),
    pressSlow: num(raw.psl),
    sg: num(raw.sg),
    abv: num(raw.abv),
    rate: num(raw.r),
    flags: int(raw.f ?? 0, 0, 0xffffffff),
    volume: num(raw.vol),
    co2Mass: num(raw.co2),
    reliefsPerHour: num(raw.rph),
    syntheticStart: raw.ss === undefined ? null : int(raw.ss, MIN_EPOCH, nowUtc + 86400),
  };
  return Object.values(record).some(value => value === undefined) ? null : record as LogRecord;
}

// Splits an NDJSON body; blank lines are skipped.
export function parseBody(body: string, nowUtc: number): { records: LogRecord[]; rejected: number } {
  const records: LogRecord[] = [];
  let rejected = 0;
  for (const line of body.split('\n')) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    const record = parseRecord(trimmed, nowUtc);
    if (record) records.push(record);
    else rejected++;
  }
  return { records, rejected };
}
