// Server-rendered pages of the cloud itself (the Povoto pages are static
// assets). Plain HTML, in English like the Povoto, styled like its graphs page.
import type { BatchRow, PovotoRow, ShareRow } from './db';
import { ageText, batchLabel, localDateTime } from './format';

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
.button, button, input { border: 1px solid #454e5e; border-radius: 7px; background: #29313d; color: #f2f5fa; padding: 8px 12px; font: inherit; text-decoration: none; }
button { cursor: pointer; }
button.danger { border-color: #8a3b3b; background: #3a2324; }
form.inline { display: inline; margin: 0; }
input[type=search] { min-width: 0; flex: 1; }
.share { font-size: 13px; word-break: break-all; }
.signature { display: flex; justify-content: center; margin-top: 34px; }
.signature img { width: 160px; max-width: 45%; height: auto; filter: invert(1); opacity: .7; }
`;

function layout(title: string, body: string): string {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title><link rel="icon" href="/assets/povoto.svg">
<style>${STYLE}</style></head>
<body><main>${body}
<footer class="signature"><img src="/assets/brewtal.svg" alt="Brewtal"></footer></main></body></html>`;
}

function brandTitle(text: string): string {
  return `<h1 class="brand"><img src="/assets/povoto.svg" alt="">${escapeHtml(text)}</h1>`;
}

function searchForm(query = ''): string {
  return `<form class="row" action="/search" method="get" role="search">
<input type="search" name="q" value="${escapeHtml(query)}" placeholder="Batch number or name" aria-label="Search batches">
<button type="submit">Search</button></form>`;
}

// The site appears only when the list has Povotos from more than one site.
export function povotoName(povoto: { site: number; num: number }, withSite = true): string {
  return withSite ? `Povoto ${povoto.num} · site ${povoto.site}` : `Povoto ${povoto.num}`;
}

export function homePage(email: string, povotos: PovotoRow[], nowLocal: number): string {
  const withSite = new Set(povotos.map(p => p.site)).size > 1;
  const cards = povotos.map(p => {
    const age = p.last_epoch === null ? null : Math.max(0, nowLocal - p.last_epoch);
    const active = age !== null && age <= ACTIVE_SECONDS && p.mode !== null && p.mode in MODE_NAMES;
    const batch = active ? `<div>Batch ${p.batch} · ${escapeHtml(p.batch_name || 'no name')}</div>
<div class="muted">${MODE_NAMES[p.mode!]} · updated ${ageText(age!)}</div>` : '';
    return `<div class="card row"><div class="grow"><div class="name">${escapeHtml(povotoName(p, withSite))}</div>
${batch}</div>
<a class="button" href="/p/${p.id}/dashboard/">Dashboard</a>
<a class="button" href="/p/${p.id}/">Old batches</a></div>`;
  }).join('');
  return layout('Povoto', `${brandTitle('Povoto')}${searchForm()}<h2>Fermenters</h2>
${cards || '<p class="muted">No Povoto is shared with this e-mail.</p>'}
<p class="muted">${escapeHtml(email)}</p>`);
}

export function searchPage(query: string, results: BatchRow[], names: Map<number, string>): string {
  const items = results.map(b => `<div class="card row"><div class="grow">
<strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(names.get(b.povoto_id) ?? `Povoto ${b.povoto_id}`)} ·
${escapeHtml(localDateTime(b.first_epoch))} to ${escapeHtml(localDateTime(b.last_epoch))}</div></div>
<a class="button" href="/p/${b.povoto_id}/graphs/?batch=${b.batch}">Graphs</a>
<a class="button" href="/p/${b.povoto_id}/">Old batches</a></div>`).join('');
  return layout('Search', `<p><a href="/">&larr; Povotos</a></p>${brandTitle('Search')}${searchForm(query)}
<h2>Results</h2>${items || '<p class="muted">No batch found.</p>'}`);
}

export function povotoPage(povoto: { id: number; site: number; num: number }, batches: BatchRow[],
                           shares: ShareRow[], canEdit: boolean, origin: string): string {
  const items = batches.map(b => {
    const links = shares.filter(s => s.batch === b.batch).map(s => {
      const url = `${origin}/s/${s.token}/`;
      return `<div class="row share"><a href="${escapeHtml(url)}">${escapeHtml(url)}</a>
<form class="inline" method="post" action="/p/${povoto.id}/shares/${escapeHtml(s.token)}/revoke">
<button type="submit">Revoke</button></form></div>`;
    }).join('');
    const actions = canEdit ? `
<form class="inline" method="post" action="/p/${povoto.id}/batches/${b.batch}/share">
<button type="submit">Create public link</button></form>
<form class="inline" method="post" action="/p/${povoto.id}/batches/${b.batch}/delete"
 onsubmit="return confirm('Delete batch ${b.batch} and all its cloud data? This cannot be undone.')">
<input type="hidden" name="confirm" value="${b.batch}">
<button class="danger" type="submit">Delete</button></form>` : '';
    return `<div class="card"><div class="row"><div class="grow"><strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(localDateTime(b.first_epoch))} to ${escapeHtml(localDateTime(b.last_epoch))}</div></div>
<a class="button" href="graphs/?batch=${b.batch}">Graphs</a>${actions}</div>${links}</div>`;
  }).join('');
  return layout(povotoName(povoto), `<p><a href="/">&larr; Povotos</a></p>
${brandTitle(povotoName(povoto))}
<div class="row"><a class="button" href="dashboard/">Dashboard</a></div>
<h2>Batches</h2>${items || '<p class="muted">No batch recorded.</p>'}`);
}

export function messagePage(title: string, message: string): string {
  return layout(title, `${brandTitle(title)}<p>${escapeHtml(message)}</p><p><a href="/">Home</a></p>`);
}
