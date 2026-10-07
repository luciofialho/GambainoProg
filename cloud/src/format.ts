// Responses in the same formats as the Povoto's own endpoints, so its
// /dashboard and /graphs pages run unchanged (Povoto/src/PovotoPages.cpp).
import type { BatchRow, LogRow } from './db';

const MODE_FERMENTING = 2;

function fixed(value: number | null, decimals: number, missing: string): string {
  return value === null ? missing : value.toFixed(decimals);
}

// formatBatchDateShort() in Povoto_UI.cpp.
export function batchDateShort(date: string): string {
  let match = /^(\d{1,2})\/(\d{1,2})/.exec(date) ?? /^(\d{1,2})-(\d{1,2})$/.exec(date);
  if (match) return `${match[1].padStart(2, '0')}/${match[2].padStart(2, '0')}`;
  match = /^(\d{4})-(\d{1,2})-(\d{1,2})/.exec(date);
  if (match) return `${match[3].padStart(2, '0')}/${match[2].padStart(2, '0')}`;
  return 'n/a';
}

// Local epoch as the device prints it (formatLocalEpochISO in datalog.cpp).
export function localIso(epoch: number): string {
  return new Date(epoch * 1000).toISOString().slice(0, 19);
}

export function localDateTime(epoch: number): string {
  return localIso(epoch).replace('T', ' ').slice(0, 16);
}

export function ageText(seconds: number): string {
  if (seconds < 90) return 'now';
  if (seconds < 90 * 60) return `${Math.round(seconds / 60)} min ago`;
  if (seconds < 36 * 3600) return `${Math.round(seconds / 3600)} h ago`;
  return `${Math.round(seconds / 86400)} days ago`;
}

// handleDashboardStatus(): texts formatted as the device sends them. The
// page shows `updated` below the screen instead of the WiFi indicator; the
// cloud is read-only.
export function dashboardStatus(log: LogRow, batch: BatchRow | null, num: number, nowLocal: number) {
  // gCO2/L/d with the graph's rule (null outside Fermenting or in a transition),
  // in the device's format (handleDashboardStatus).
  const co2Label = log.mode !== MODE_FERMENTING ? 'g CO2'
    : log.rate === null ? 'gCO2 [N/A/L.d]' : `gCO2 [${log.rate.toFixed(1)}/(L.d)]`;
  return {
    batchNumber: String(log.batch).padStart(4, '0'),
    batchDate: batchDateShort(batch?.date ?? ''),
    povotoNum: String(num),
    batchName: batch?.name ?? '',
    ogLabel: `SG   [ OG=${fixed(batch?.og ?? null, 3, 'n/a')} ]`,
    temperature: fixed(log.temp, 1, 'ERR'),
    tempTarget: fixed(log.temp_sp, 1, 'n/a'),
    slowTarget: fixed(log.temp_slow, 1, 'n/a'),
    pressure: fixed(log.press, 1, 'ERR'),
    pressureTarget: log.press_sp ? log.press_sp.toFixed(2) : 'n/a',
    co2Label,
    co2Mass: fixed(log.co2_mass, 0, ''),
    volume: fixed(log.volume, 0, ''),
    sg: fixed(log.sg, 3, ''),
    abv: fixed(log.abv, 2, ''),
    wifiText: '',
    updated: `Updated ${ageText(Math.max(0, nowLocal - log.epoch))}`,
    wifiBars: 0,
    calibration: ['', '', ''],
    readOnly: true,
    back: { href: '/povotos', label: 'Back' },
    setpointLink: true,   // the set point touch zones open /p/<id>/setpoint
  };
}

function csvNumber(value: number | null, decimals: number): string {
  return value === null ? '' : value.toFixed(decimals).replace('.', ',');
}

// handleGraphsCSV(): semicolons, decimal commas, blank for missing values.
export function graphCsv(rows: LogRow[]): string {
  const lines = ['epoch_local;datetime_local;SG;TempC;TempSetpointC;PressureBar;' +
    'PressureSetpointBar;gCO2_L_d;ABV_percent;flags'];
  for (const row of rows) {
    lines.push([row.epoch, localIso(row.epoch), csvNumber(row.sg, 5), csvNumber(row.temp, 2),
      csvNumber(row.temp_sp, 2), csvNumber(row.press, 3), csvNumber(row.press_sp, 3),
      csvNumber(row.rate, 3), csvNumber(row.abv, 2), row.flags].join(';'));
  }
  return lines.join('\r\n') + '\r\n';
}

function hoursMinutes(seconds: number | null): string {
  if (seconds === null) return '-';
  const minutes = Math.floor(seconds / 60);
  return `${Math.floor(minutes / 60)}h ${String(minutes % 60).padStart(2, '0')}m`;
}

function mol(value: number | null): string {
  return value === null ? '-' : value.toFixed(2);
}

// Latest batch state (counters), or '' when none was received.
export function batchStateText(batch: BatchRow): string {
  if (batch.state_epoch === null) return '';
  return `Chiller ${hoursMinutes(batch.chill_seconds)} · Heater ${hoursMinutes(batch.heat_seconds)}` +
    ` · Expansions ${batch.expansions ?? '-'} · CO2 mol: headspace ${mol(batch.mol_headspace)},` +
    ` dissolved ${mol(batch.mol_dissolved)}, ejected ${mol(batch.mol_ejected)}` +
    ` · Dumped ${batch.dumped_volume === null ? '-' : `${batch.dumped_volume.toFixed(1)} L`}` +
    ` (at ${localDateTime(batch.state_epoch)})`;
}

export function batchLabel(batch: BatchRow): string {
  const date = batch.date ? ` (${batch.date})` : '';
  return `${batch.batch} - ${batch.name || 'no name'}${date}`;
}
