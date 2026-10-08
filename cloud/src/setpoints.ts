// Phase 2: the set points and automatic rules of one Povoto, as its latest
// snapshots show them, with the forms that send requests to it
// (Povoto/docs/cloud-log.md). The Povoto validates and applies; the ranges
// here only catch typing mistakes early.
import type { LogRow, RequestRow, SnapshotRow, SyncRow } from './db';
import { localDateTime } from './format';
import { brandTitle, escapeHtml, layout, povotoName } from './pages';

type Values = Record<string, number | string | null>;

const RULE_COUNT = 8;

// Field: [form/JSON name, label, min, max, step]. Same ranges as the Povoto.
type Field = [string, string, number, number, string];
const SETPOINT_FIELDS: Field[] = [
  ['t', 'Temperature (°C)', 0, 42, '0.1'],
  ['ts', 'Temperature slow target (°C)', 0, 42, '0.1'],
  ['tv', 'Temperature ramp (°C/day)', 1, 8, '0.1'],
  ['p', 'Pressure (bar)', 0, 2, '0.01'],
  ['ps', 'Pressure slow target (bar)', 0, 2, '0.01'],
  ['pv', 'Pressure ramp (bar/day)', 0.1, 2, '0.01'],
];
const RULE_TRIGGERS: Field[] = [
  ['sh', 'Temp. stable for (h)', 1, 360, '0.1'],
  ['ph', 'Press. stable for (h)', 1, 360, '0.1'],
  ['sg', 'SG <', 0.99, 1.2, '0.001'],
  ['co', 'gCO2/L/d <', 0.5, 20, '0.1'],
];
const RULE_ACTIONS: Field[] = [
  ['t', 'Temperature (°C)', 0, 42, '0.1'],
  ['tsl', 'Temperature slow (°C)', 0, 42, '0.1'],
  ['p', 'Pressure (bar)', 0, 2, '0.01'],
  ['psl', 'Pressure slow (bar)', 0, 2, '0.01'],
];
// Fields the set points need (the others may stay empty).
const SETPOINT_REQUIRED = new Set(['p']);

function parse(data: string | undefined): Values {
  try {
    return data ? JSON.parse(data) as Values : {};
  } catch {
    return {};
  }
}

// Form value → number, null (empty) or an error message.
export function readField(form: Record<string, unknown>, field: Field, required: boolean): number | null | string {
  const [name, label, min, max] = field;
  const raw = typeof form[name] === 'string' ? (form[name] as string).trim().replace(',', '.') : '';
  if (!raw) return required ? `${label} is required.` : null;
  const value = Number(raw);
  if (!Number.isFinite(value) || value < min || value > max) return `${label} must be from ${min} to ${max}.`;
  return value;
}

// Builds the request data from the form; a string is the error to show.
export function setpointRequest(form: Record<string, unknown>): Values | string {
  const data: Values = {};
  for (const field of SETPOINT_FIELDS) {
    const value = readField(form, field, SETPOINT_REQUIRED.has(field[0]));
    if (typeof value === 'string') return value;
    data[field[0]] = value;
  }
  return data;
}

export function ruleRequest(form: Record<string, unknown>, index: number): Values | string {
  const name = typeof form.n === 'string' ? form.n.trim().slice(0, 80) : '';
  const data: Values = { i: index, n: name };
  for (const field of [...RULE_TRIGGERS, ...RULE_ACTIONS]) {
    const value = readField(form, field, false);
    if (typeof value === 'string') return value;
    data[field[0]] = value;
  }
  data.rq = index > 0 && form.rq === '1' ? 1 : 0;
  data.mo = form.mo === '1' ? 1 : 0;
  return data;
}

function numberInput(field: Field, value: unknown, disabled: boolean): string {
  const [name, label, min, max, step] = field;
  const text = typeof value === 'number' ? String(value) : '';
  return `<label class="field">${escapeHtml(label)}<input type="number" name="${name}" value="${escapeHtml(text)}"
 min="${min}" max="${max}" step="${step}"${disabled ? ' disabled' : ''}></label>`;
}

function ruleIsEmpty(rule: Values): boolean {
  return !rule.n && !rule.mo && [...RULE_TRIGGERS, ...RULE_ACTIONS].every(([name]) => rule[name] === null || rule[name] === undefined);
}

const KIND_NAMES: Record<string, string> = {
  sp: 'Set points', rule: 'Rule', reset: 'Reset rule', trigger: 'Trigger rule now',
};

function requestText(r: RequestRow, withAuthor = true): string {
  const data = parse(r.data);
  const what = KIND_NAMES[r.kind] ?? r.kind;
  const rule = r.kind !== 'sp' && typeof data.i === 'number' ? ` ${data.i + 1}` : '';
  const status = r.status === 'unconfirmed' ? 'no answer from the Povoto'
    : r.status === 'expired' ? 'not delivered (SideKick offline?)' : r.status;
  const message = r.message ? `: ${r.message}` : '';
  const author = withAuthor ? ` · ${escapeHtml(r.created_by)}` : '';
  return `${escapeHtml(what + rule)}${author} · ${escapeHtml(status + message)}`;
}

// The values a request sent, shown instead of the copy while the Povoto has
// not answered yet, or has applied them but its new copy has not arrived (the
// copy still has the version the request was made on). Rejected, no answer or
// a newer copy: the Povoto's data. requests: newest first.
function requestedValues(requests: RequestRow[], match: (r: RequestRow) => boolean,
                         copyHash: string | undefined): Values | null {
  const request = requests.find(match);
  if (!request) return null;
  if (request.status === 'pending' || request.status === 'sent') return parse(request.data);
  if (request.status === 'applied' && request.base_hash === copyHash) return parse(request.data);
  return null;
}

export interface SetpointPageInput {
  povoto: { id: number; num: number };
  sync: SyncRow | null;
  setpoints: SnapshotRow | null;
  rules: SnapshotRow[];
  latest: LogRow | null;
  requests: RequestRow[];
  open: RequestRow | null;
  canEdit: boolean;
  // Set when this cloud may not send requests to the Povoto (a production
  // site seen from the development cloud): the values show, the forms don't.
  writeBlocked: string | null;
  message: string;
  localOffsetSeconds: number; // request times are UTC
}

export function setpointPage(input: SetpointPageInput): string {
  const { povoto, sync, setpoints, latest, open, canEdit } = input;
  const rules = new Map(input.rules.map(r => [r.idx, r]));
  const rulesInSync = !!sync?.rules_hash && input.rules.length === RULE_COUNT &&
    input.rules.every(r => r.hash === sync.rules_hash);
  const setpointsInSync = !!sync?.setpoint_hash && setpoints?.hash === sync.setpoint_hash;

  const notes: string[] = [];
  if (input.message) notes.push(`<div class="note">${escapeHtml(input.message)}</div>`);
  if (input.writeBlocked) notes.push(`<div class="note warn">${escapeHtml(input.writeBlocked)}</div>`);
  if (!sync) {
    notes.push('<div class="note warn">This Povoto has not reported its set points yet: it needs the cloud edits firmware and a batch running.</div>');
  } else {
    if (sync.edits_accepted === 0) notes.push('<div class="note warn">Cloud edits are off on this Povoto (Settings page on the Povoto).</div>');
    if (!setpointsInSync || !rulesInSync) notes.push('<div class="note warn">Waiting for the Povoto to send its current copy; editing resumes when it arrives.</div>');
  }
  if (open) notes.push(`<div class="note">Waiting for the Povoto: ${requestText(open, false)}.</div>`);
  // While a request or a new copy is awaited the forms are disabled, so
  // reloading loses nothing.
  if (open || (sync && (!setpointsInSync || !rulesInSync))) {
    notes.push('<script>setTimeout(() => location.replace(location.pathname), 5000);</script>');
  }

  const editable = canEdit && !input.writeBlocked && sync?.edits_accepted === 1 && !open;
  const spEditable = editable && setpointsInSync;
  const rulesEditable = editable && rulesInSync;

  // During a ramp the snapshot leaves the direct value out; the latest log has it.
  const sp = requestedValues(input.requests, r => r.kind === 'sp', setpoints?.hash) ?? parse(setpoints?.data);
  if (sp.t === null || sp.t === undefined) sp.t = latest?.temp_sp ?? null;
  if (sp.p === null || sp.p === undefined) sp.p = latest?.press_sp ?? null;
  const setpointForm = setpoints ? `<form method="post" action="setpoint">
<input type="hidden" name="base" value="${escapeHtml(setpoints.hash)}">
<div class="grid">${SETPOINT_FIELDS.map(f => numberInput(f, sp[f[0]], !spEditable)).join('')}</div>
<p class="muted">As on the Povoto: a direct value alone cancels a ramp; a slow target ramps from the direct value. Empty temperature = no temperature control.</p>
${canEdit ? `<button type="submit"${spEditable ? '' : ' disabled'}>Send set points</button>` : ''}</form>`
    : '<p class="muted">No copy yet.</p>';

  // Rules: rule 1 always; each later rule after the last non-empty one plus one.
  const ruleValues = Array.from({ length: RULE_COUNT }, (_, i) => {
    const copy = parse(rules.get(i)?.data);
    const sent = requestedValues(input.requests, r => r.kind === 'rule' && parse(r.data).i === i, rules.get(i)?.hash);
    return sent ? { ...sent, at: copy.at ?? 0 } : copy;
  });
  let shown = 1;
  ruleValues.forEach((rule, i) => { if (!ruleIsEmpty(rule)) shown = Math.min(RULE_COUNT, i + 2); });
  const rulesHash = sync?.rules_hash ?? '';
  const ruleCards = ruleValues.slice(0, shown).map((rule, i) => {
    const triggeredAt = typeof rule.at === 'number' && rule.at > 0 ? rule.at : 0;
    const disabled = !rulesEditable || triggeredAt > 0;
    const status = triggeredAt ? `<p class="note ok">Triggered ${escapeHtml(localDateTime(triggeredAt))}</p>` : '';
    const checks = `${i > 0 ? `<label class="check"><input type="checkbox" name="rq" value="1"${rule.rq ? ' checked' : ''}${disabled ? ' disabled' : ''}>Requires rule ${i}</label> ` : ''}
<label class="check"><input type="checkbox" name="mo" value="1"${rule.mo ? ' checked' : ''}${disabled ? ' disabled' : ''}>Manual only</label>`;
    const base = `<input type="hidden" name="base" value="${escapeHtml(rulesHash)}">`;
    const actions = !canEdit ? '' : triggeredAt
      ? `<form class="inline" method="post" action="setpoint/reset/${i}" onsubmit="return confirm('Reset rule ${i + 1}?')">${base}
<button type="submit"${rulesEditable ? '' : ' disabled'}>Reset</button></form>`
      : `<button type="submit" form="rule${i}"${rulesEditable ? '' : ' disabled'}>Save rule</button>
${ruleIsEmpty(rule) ? '' : `<form class="inline" method="post" action="setpoint/trigger/${i}" onsubmit="return confirm('Trigger rule ${i + 1} now?')">${base}
<button type="submit"${rulesEditable ? '' : ' disabled'}>Trigger now</button></form>`}`;
    return `<fieldset><legend>Rule ${i + 1}</legend>${status}
<form id="rule${i}" method="post" action="setpoint/rule/${i}">${base}
<label class="field wide">Name<input type="text" name="n" maxlength="80" value="${escapeHtml(rule.n ?? '')}"${disabled ? ' disabled' : ''}></label>
<div class="muted">Triggers (all filled ones)</div>
<div class="grid">${RULE_TRIGGERS.map(f => numberInput(f, rule[f[0]], disabled)).join('')}</div>
<div class="muted">New set points</div>
<div class="grid">${RULE_ACTIONS.map(f => numberInput(f, rule[f[0]], disabled)).join('')}</div>
${checks}</form>
<div class="row" style="margin-top:10px">${actions}</div></fieldset>`;
  }).join('');

  const history = input.requests.length
    ? input.requests.map(r => `<div class="muted">${escapeHtml(localDateTime(r.created_at + input.localOffsetSeconds))} · ${requestText(r)}</div>`).join('')
    : '<p class="muted">None yet.</p>';

  return layout(`${povotoName(povoto)} · Set points`, `<p><a href="dashboard/">&larr; Back</a></p>
${brandTitle(`${povotoName(povoto)} · Set points`)}${notes.join('')}
<h2>Set points</h2><div class="card">${setpointForm}</div>
<h2>Automatic set points</h2>${rules.size ? ruleCards : '<p class="muted">No copy yet.</p>'}
<h2>Recent requests</h2><div class="card">${history}</div>`);
}
