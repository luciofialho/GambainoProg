// Server-rendered pages of the cloud itself (the Povoto pages are static
// assets). Plain HTML, in English like the Povoto, styled like its graphs page.
import type { BatchRow, PovotoRow, ShareRow } from './db';
import { ageText, batchLabel, batchStateText, localDateTime } from './format';

// A Povoto is shown with its batch while records keep arriving in Fermenting
// or Conditioning (it sends every 5 min while a batch is on).
const ACTIVE_SECONDS = 30 * 60;
const MODE_NAMES: Record<number, string> = { 2: 'Fermenting', 3: 'Conditioning' };

export function escapeHtml(value: unknown): string {
  return String(value ?? '').replace(/[&<>"']/g, char =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]!);
}

const STYLE = `
:root { color-scheme: dark; font-family: Arial, sans-serif; background: #16191e; color: #e8edf3; }
body { margin: 0; }
main { max-width: 960px; margin: auto; padding: 22px 16px; }
h1 { font-size: 26px; margin: 0 0 18px; }
h2 { font-size: 18px; margin: 26px 0 10px; }
a { color: #a9c4f5; }
.brand { display: flex; align-items: center; gap: 12px; }
.brand img { width: 52px; height: 52px; object-fit: contain; filter: invert(1); opacity: .9; }
.card { background: #22272e; border: 1px solid #343d48; border-radius: 12px; padding: 14px 16px; margin-bottom: 12px; }
.row { display: flex; flex-wrap: wrap; align-items: center; gap: 10px 16px; }
.grow { flex: 1; min-width: 200px; }
.name { font-size: 18px; font-weight: bold; }
.muted { color: #aeb9c6; font-size: 13px; }
.button, button, input, select { border: 1px solid #454e5e; border-radius: 7px; background: #29313d; color: #f2f5fa; padding: 8px 12px; font: inherit; text-decoration: none; }
button { cursor: pointer; }
button:disabled { opacity: .45; cursor: not-allowed; }
form.inline { display: inline; margin: 0; }
input[type=search] { min-width: 0; flex: 1; }
.share { font-size: 13px; word-break: break-all; }
.sites { margin-bottom: 14px; } .sites .admin { margin-left: auto; }
.button.selected { background: #435e9a; border-color: #6e91dd; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 10px 14px; margin: 10px 0; }
label.field { display: flex; flex-direction: column; gap: 4px; font-size: 13px; color: #aeb9c6; }
label.check { font-size: 14px; color: #e8edf3; }
label.check input { margin-right: 6px; }
.grid input, .grid select, .wide input { width: 100%; box-sizing: border-box; }
input:disabled { opacity: .6; }
.note { padding: 10px 14px; border-radius: 8px; margin-bottom: 12px; background: #2a3140; }
.note.warn { background: #3a2f1e; }
.note.ok { background: #1f3326; }
fieldset { border: 1px solid #343d48; border-radius: 12px; padding: 12px 16px; margin: 0 0 12px; background: #22272e; }
legend { font-weight: bold; padding: 0 6px; }
.signature { display: flex; justify-content: center; margin-top: 34px; }
.signature img { width: 160px; max-width: 45%; height: auto; filter: invert(1); opacity: .7; }
`;

// "DEV" on the development cloud (CLOUD_ENV): in the tab title and a strip on
// every page, so it is never taken for production. One value per Worker.
let environmentLabel = '';
export function setEnvironmentLabel(label: string): void {
  environmentLabel = label;
}

export function layout(title: string, body: string): string {
  const strip = environmentLabel
    ? `<div style="background:#b5651d;color:#fff;text-align:center;font-weight:bold;padding:4px">${escapeHtml(environmentLabel)} cloud</div>`
    : '';
  return `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${environmentLabel ? `[${escapeHtml(environmentLabel)}] ` : ''}${escapeHtml(title)}</title><link rel="icon" href="/assets/povoto.svg">
<style>${STYLE}</style></head>
<body>${strip}<main>${body}
<footer class="signature"><img src="/assets/brewtal.svg" alt="Brewtal"></footer></main></body></html>`;
}

export function brandTitle(text: string): string {
  return `<h1 class="brand"><img src="/assets/povoto.svg" alt="">${escapeHtml(text)}</h1>`;
}

function searchForm(query = ''): string {
  return `<form class="row" action="/search" method="get" role="search">
<input type="search" name="q" value="${escapeHtml(query)}" placeholder="Batch number or name" aria-label="Search batches">
<button type="submit">Search</button></form>`;
}

// Each user sees the Povotos of one site, so the site is not shown.
export function povotoName(povoto: { num: number }): string {
  return `Povoto ${povoto.num}`;
}

// The batch a Povoto is running: its latest record is recent and in
// Fermenting or Conditioning. null otherwise.
export function currentBatch(latest: { epoch: number; mode: number; batch: number } | null,
                             nowLocal: number): number | null {
  if (!latest || !(latest.mode in MODE_NAMES)) return null;
  return nowLocal - latest.epoch <= ACTIVE_SECONDS ? latest.batch : null;
}

// With Povotos of more than one site: a site filter at the top (selected =
// null shows all), and each card names its site.
export function homePage(email: string, povotos: PovotoRow[], nowLocal: number,
                         siteNames: Map<number, string>, selected: number | null, isAdmin: boolean): string {
  const sites = [...new Set(povotos.map(p => p.site))].sort((a, b) => a - b);
  const siteName = (site: number) => siteNames.get(site) || `Site ${site}`;
  const siteButtons = sites.length < 2 ? '' : [null, ...sites].map(site => {
    const href = site === null ? '/povotos?site=all' : `/povotos?site=${site}`;
    const label = site === null ? 'All sites' : siteName(site);
    const current = site === selected;
    return `<a class="button${current ? ' selected' : ''}" href="${href}"${current ? ' aria-current="page"' : ''}>${escapeHtml(label)}</a>`;
  }).join('');
  // Administrators: the users page, at the right of the site buttons.
  const admin = isAdmin ? '<a class="button admin" href="/admin">Users</a>' : '';
  const filter = siteButtons || admin ? `<nav class="row sites" aria-label="Site">${siteButtons}${admin}</nav>` : '';
  const shown = selected === null || !sites.includes(selected) ? povotos : povotos.filter(p => p.site === selected);
  const showSite = sites.length > 1 && (selected === null || !sites.includes(selected));
  const cards = shown.map(p => {
    const latest = p.last_epoch === null || p.mode === null || p.batch === null ? null
      : { epoch: p.last_epoch, mode: p.mode, batch: p.batch };
    const batch = currentBatch(latest, nowLocal) === null ? ''
      : `<div>Batch ${p.batch} · ${escapeHtml(p.batch_name || 'no name')}</div>
<div class="muted">${MODE_NAMES[p.mode!]} · updated ${ageText(Math.max(0, nowLocal - p.last_epoch!))}</div>`;
    const site = showSite ? ` <span class="muted">${escapeHtml(siteName(p.site))}</span>` : '';
    return `<div class="card row"><div class="grow"><div class="name">${escapeHtml(povotoName(p))}${site}</div>
${batch}</div>
<a class="button" href="/p/${p.id}/dashboard/">Dashboard</a>
<a class="button" href="/p/${p.id}/">All batches</a></div>`;
  }).join('');
  return layout('Povoto', `${brandTitle('Povoto')}${filter}${searchForm()}<h2>Fermenters</h2>
${cards || '<p class="muted">No Povoto is shared with this e-mail.</p>'}
<p class="muted">${escapeHtml(email)}</p>`);
}

export function searchPage(query: string, results: BatchRow[], names: Map<number, string>): string {
  const items = results.map(b => `<div class="card row"><div class="grow">
<strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(names.get(b.povoto_id) ?? `Povoto ${b.povoto_id}`)} ·
${escapeHtml(localDateTime(b.first_epoch))} to ${escapeHtml(localDateTime(b.last_epoch))}</div></div>
<a class="button" href="/p/${b.povoto_id}/graphs/?batch=${b.batch}">Graphs</a>
<a class="button" href="/p/${b.povoto_id}/">All batches</a></div>`).join('');
  return layout('Search', `<p><a href="/povotos">&larr; Back</a></p>${brandTitle('Search')}${searchForm(query)}
<h2>Results</h2>${items || '<p class="muted">No batch found.</p>'}`);
}

// The running batch opens the dashboard, the others their graphs. One public
// link per batch: the button is disabled while one exists. (Deleting a batch
// is kept in the routes, without a button for now.)
export function povotoPage(povoto: { id: number; num: number }, batches: BatchRow[],
                           shares: ShareRow[], canEdit: boolean, origin: string,
                           running: number | null): string {
  const items = batches.map(b => {
    const batchShares = shares.filter(s => s.batch === b.batch);
    const links = batchShares.map(s => {
      const url = `${origin}/s/${s.token}/`;
      return `<div class="row share"><a href="${escapeHtml(url)}">${escapeHtml(url)}</a>
<form class="inline" method="post" action="/p/${povoto.id}/shares/${escapeHtml(s.token)}/revoke">
<button type="submit">Revoke</button></form></div>`;
    }).join('');
    const share = !canEdit ? '' : batchShares.length
      ? '<button type="button" disabled title="This batch already has a public link">Create public link</button>'
      : `<form class="inline" method="post" action="/p/${povoto.id}/batches/${b.batch}/share">
<button type="submit">Create public link</button></form>`;
    const open = b.batch === running
      ? '<a class="button" href="dashboard/">Dashboard</a>'
      : `<a class="button" href="graphs/?batch=${b.batch}">Graphs</a>`;
    return `<div class="card"><div class="row"><div class="grow"><strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(localDateTime(b.first_epoch))} to ${escapeHtml(localDateTime(b.last_epoch))}</div>
${batchStateText(b) ? `<div class="muted">${escapeHtml(batchStateText(b))}</div>` : ''}</div>
${open}${share}</div>${links}</div>`;
  }).join('');
  return layout(povotoName(povoto), `<p><a href="/povotos">&larr; Back</a></p>
${brandTitle(povotoName(povoto))}
<h2>Batches</h2>${items || '<p class="muted">No batch recorded.</p>'}`);
}

export function messagePage(title: string, message: string): string {
  return layout(title, `${brandTitle(title)}<p>${escapeHtml(message)}</p><p><a href="/povotos">&larr; Back</a></p>`);
}
