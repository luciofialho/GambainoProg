// Server-rendered pages of the cloud itself (the Povoto pages are static
// assets). Plain HTML, styled like the device's graphs page.
import type { BatchRow, PovotoRow, ShareRow } from './db';
import { ageText, batchLabel, localDateTime } from './format';

export function escapeHtml(value: unknown): string {
  return String(value ?? '').replace(/[&<>"']/g, char =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]!);
}

const STYLE = `
:root { color-scheme: dark; font-family: Arial, sans-serif; background: #16191e; color: #e8edf3; }
body { margin: 0; }
main { max-width: 960px; margin: auto; padding: 22px 16px; }
h1 { font-size: 24px; margin: 0 0 18px; }
h2 { font-size: 18px; margin: 26px 0 10px; }
a { color: #a9c4f5; }
.card { background: #22272e; border: 1px solid #343d48; border-radius: 12px; padding: 14px 16px; margin-bottom: 12px; }
.row { display: flex; flex-wrap: wrap; align-items: center; gap: 10px 16px; }
.grow { flex: 1; min-width: 200px; }
.muted { color: #aeb9c6; font-size: 13px; }
.button, button, input { border: 1px solid #454e5e; border-radius: 7px; background: #29313d; color: #f2f5fa; padding: 8px 12px; font: inherit; text-decoration: none; }
button { cursor: pointer; }
button.danger { border-color: #8a3b3b; background: #3a2324; }
form.inline { display: inline; margin: 0; }
input[type=search] { min-width: 0; flex: 1; }
.share { font-size: 13px; word-break: break-all; }
`;

function layout(title: string, body: string): string {
  return `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title><style>${STYLE}</style></head>
<body><main>${body}</main></body></html>`;
}

function searchForm(query = ''): string {
  return `<form class="row" action="/search" method="get" role="search">
<input type="search" name="q" value="${escapeHtml(query)}" placeholder="Número ou nome do batch" aria-label="Buscar batch">
<button type="submit">Buscar</button></form>`;
}

export function povotoName(povoto: { site: number; num: number }): string {
  return `Povoto ${povoto.num} · local ${povoto.site}`;
}

export function homePage(email: string, povotos: PovotoRow[], nowLocal: number): string {
  const cards = povotos.map(p => {
    const last = p.last_epoch === null ? 'sem dados'
      : `atualizado ${ageText(Math.max(0, nowLocal - p.last_epoch))}`;
    const batch = p.batch === null ? '' : `Batch ${p.batch} ${escapeHtml(p.batch_name ?? '')} · `;
    return `<div class="card row"><div class="grow"><strong>${escapeHtml(povotoName(p))}</strong>
<div class="muted">${batch}${last}</div></div>
<a class="button" href="/p/${p.id}/dashboard/">Dashboard</a>
<a class="button" href="/p/${p.id}/graphs/">Gráficos</a>
<a class="button" href="/p/${p.id}/">Batches</a></div>`;
  }).join('');
  return layout('Povotos', `<h1>Povotos</h1>${searchForm()}<h2>Fermentadores</h2>
${cards || '<p class="muted">Nenhum Povoto liberado para este e-mail.</p>'}
<p class="muted">${escapeHtml(email)}</p>`);
}

export function searchPage(query: string, results: BatchRow[], names: Map<number, string>): string {
  const items = results.map(b => `<div class="card row"><div class="grow">
<strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(names.get(b.povoto_id) ?? `Povoto ${b.povoto_id}`)} ·
${escapeHtml(localDateTime(b.first_epoch))} a ${escapeHtml(localDateTime(b.last_epoch))}</div></div>
<a class="button" href="/p/${b.povoto_id}/graphs/?batch=${b.batch}">Gráficos</a>
<a class="button" href="/p/${b.povoto_id}/">Batches</a></div>`).join('');
  return layout('Busca', `<p><a href="/">&larr; Povotos</a></p><h1>Busca</h1>${searchForm(query)}
<h2>Resultados</h2>${items || '<p class="muted">Nenhum batch encontrado.</p>'}`);
}

export function povotoPage(povoto: { id: number; site: number; num: number }, batches: BatchRow[],
                           shares: ShareRow[], canEdit: boolean, origin: string): string {
  const items = batches.map(b => {
    const links = shares.filter(s => s.batch === b.batch).map(s => {
      const url = `${origin}/s/${s.token}/`;
      return `<div class="row share"><a href="${escapeHtml(url)}">${escapeHtml(url)}</a>
<form class="inline" method="post" action="/p/${povoto.id}/shares/${escapeHtml(s.token)}/revoke">
<button type="submit">Revogar</button></form></div>`;
    }).join('');
    const actions = canEdit ? `
<form class="inline" method="post" action="/p/${povoto.id}/batches/${b.batch}/share">
<button type="submit">Criar link público</button></form>
<form class="inline" method="post" action="/p/${povoto.id}/batches/${b.batch}/delete"
 onsubmit="return confirm('Apagar o batch ${b.batch} e todos os seus dados na nuvem? Não há como desfazer.')">
<input type="hidden" name="confirm" value="${b.batch}">
<button class="danger" type="submit">Apagar</button></form>` : '';
    return `<div class="card"><div class="row"><div class="grow"><strong>${escapeHtml(batchLabel(b))}</strong>
<div class="muted">${escapeHtml(localDateTime(b.first_epoch))} a ${escapeHtml(localDateTime(b.last_epoch))}</div></div>
<a class="button" href="graphs/?batch=${b.batch}">Gráficos</a>${actions}</div>${links}</div>`;
  }).join('');
  return layout(povotoName(povoto), `<p><a href="/">&larr; Povotos</a></p>
<h1>${escapeHtml(povotoName(povoto))}</h1>
<div class="row"><a class="button" href="dashboard/">Dashboard</a><a class="button" href="graphs/">Gráficos</a></div>
<h2>Batches</h2>${items || '<p class="muted">Nenhum batch gravado.</p>'}`);
}

export function messagePage(title: string, message: string): string {
  return layout(title, `<h1>${escapeHtml(title)}</h1><p>${escapeHtml(message)}</p><p><a href="/">Início</a></p>`);
}
