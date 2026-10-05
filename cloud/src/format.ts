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
  if (seconds < 90) return 'agora';
  if (seconds < 90 * 60) return `há ${Math.round(seconds / 60)} min`;
  if (seconds < 36 * 3600) return `há ${Math.round(seconds / 3600)} h`;
  return `há ${Math.round(seconds / 86400)} dias`;
}

// handleDashboardStatus(): texts formatted as the TFT draws them. The WiFi
// slot shows the age of the data instead; the cloud is read-only.
export function dashboardStatus(log: LogRow, batch: BatchRow | null, num: number, nowLocal: number) {
  const rph = log.mode === MODE_FERMENTING
    ? (log.reliefs_per_hour === null ? ' RPH:N/A' : ` RPH:${log.reliefs_per_hour.toFixed(1)}`)
    : '';
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
    co2Label: `g CO2${rph}`,
    co2Mass: fixed(log.co2_mass, 0, ''),
    volume: fixed(log.volume, 0, ''),
    sg: fixed(log.sg, 3, ''),
    abv: fixed(log.abv, 2, ''),
    wifiText: `Atualizado ${ageText(Math.max(0, nowLocal - log.epoch))}`,
    wifiBars: 0,
    calibration: ['', '', ''],
    readOnly: true,
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

export function batchLabel(batch: BatchRow): string {
  const date = batch.date ? ` (${batch.date})` : '';
  return `${batch.batch} - ${batch.name || 'sem nome'}${date}`;
}
