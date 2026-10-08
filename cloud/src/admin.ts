// Permissions page (/admin), for administrators (edit on every site). Access
// is given per site or to every site; rows for a single Povoto, if any, are
// listed and can be removed.
import type { AccessReport } from './accessGroup';
import type { PermissionRow } from './db';
import { brandTitle, escapeHtml, layout } from './pages';

function scopeName(povotoId: number, sites: Map<number, string>): string {
  if (povotoId === 0) return 'All sites';
  if (povotoId % 100 === 0) return sites.get(povotoId / 100) || `Site ${povotoId / 100}`;
  return `Povoto ${povotoId % 100} · ${sites.get(Math.floor(povotoId / 100)) || `site ${Math.floor(povotoId / 100)}`}`;
}

// What Cloudflare Access really does: who the policy lets in, and who holds a
// seat (with Release for people without access here).
function accessSection(report: AccessReport, permitted: Set<string>): string {
  const problems = report.problems.map(p => `<div class="note warn">${escapeHtml(p)}</div>`).join('');
  const rules = report.rules.length
    ? report.rules.map(r => `<div>${escapeHtml(r)}${r.includes('@') || permitted.has(r) ? '' : ' <span class="muted">(not an e-mail: lets more people in)</span>'}</div>`).join('')
    : '<p class="muted">Nothing.</p>';
  const seats = report.seats.length ? report.seats.map(s => {
    const release = permitted.has(s.email) ? '<span class="muted">has access</span>'
      : `<form class="inline" method="post" action="/admin/release">
<input type="hidden" name="email" value="${escapeHtml(s.email)}"><button type="submit">Release</button></form>`;
    return `<div class="row"><div class="grow">${escapeHtml(s.email)} <span class="muted">${escapeHtml(s.lastLogin)}</span></div>${release}</div>`;
  }).join('') : '<p class="muted">None.</p>';
  return `<h2>Cloudflare Access</h2>${problems}
<div class="card"><div class="muted">Policy "Povoto users" lets in:</div>${rules}</div>
<div class="card"><div class="muted">Seats in use (of 50 free):</div>${seats}</div>`;
}

export function adminPage(permissions: PermissionRow[], sites: Map<number, string>, ownEmail: string,
                          message: string, report: AccessReport): string {
  const permitted = new Set(permissions.map(p => p.email.toLowerCase()));
  const siteOptions = [...sites.entries()].sort((a, b) => a[0] - b[0])
    .map(([site, name]) => `<option value="${site * 100}">${escapeHtml(name || `Site ${site}`)}</option>`).join('');
  const rows = permissions.map(p => {
    const own = p.email.toLowerCase() === ownEmail.toLowerCase();
    return `<div class="card row"><div class="grow"><strong>${escapeHtml(p.email)}</strong>
<div class="muted">${escapeHtml(scopeName(p.povoto_id, sites))} · ${p.role === 'edit' ? 'view and edit' : 'view only'}</div></div>
${own ? '<span class="muted">you</span>' : `<form class="inline" method="post" action="/admin/revoke"
 onsubmit="return confirm('Remove this access of ${escapeHtml(p.email)}?')">
<input type="hidden" name="email" value="${escapeHtml(p.email)}">
<input type="hidden" name="scope" value="${p.povoto_id}">
<button type="submit">Remove</button></form>`}</div>`;
  }).join('');
  return layout('Users', `<p><a href="/povotos">&larr; Back</a></p>${brandTitle('Users')}
${message ? `<div class="note">${escapeHtml(message)}</div>` : ''}
<h2>Give access</h2>
<form class="card" method="post" action="/admin/grant">
<div class="grid">
<label class="field">E-mail<input type="email" name="email" required maxlength="120"></label>
<label class="field">Site<select name="scope"><option value="0">All sites</option>${siteOptions}</select></label>
<label class="field">Role<select name="role"><option value="view">View only</option><option value="edit">View and edit</option></select></label>
</div>
<p class="muted">The person signs in with this e-mail (code by e-mail or Google). Giving an existing access again changes its role; "All sites" replaces the person's other accesses.</p>
<button type="submit">Save access</button></form>
<h2>Accesses</h2>${rows || '<p class="muted">None.</p>'}
<form class="row" method="post" action="/admin/sync" style="margin-top:14px">
<span class="muted grow">Only these e-mails can sign in: the Cloudflare Access policy "Povoto users" follows this list.</span>
<button type="submit">Sync with Access</button></form>
<form class="row" method="post" action="/admin/setup" style="margin-top:10px">
<span class="muted grow">Sets up Cloudflare Access: the "Povoto users" policy on the site pages (/povotos, /search, /p, /admin), the welcome page at the root and the public batch links (/s/...) open without login.</span>
<button type="submit">Set up Access</button></form>
${accessSection(report, permitted)}`);
}
