// Povoto cloud, phase 1 (Povoto/docs/cloud-plan.md, cloud/README.md).
import { Hono, type Context } from 'hono';
import { getCookie, setCookie } from 'hono/cookie';
import { accessEmail, randomToken, sha256Hex } from './auth';
import * as db from './db';
import { batchLabel, dashboardStatus, graphCsv } from './format';
import { currentBatch, homePage, messagePage, povotoName, povotoPage, searchPage, setEnvironmentLabel } from './pages';
import { parseBody } from './records';
import { ruleRequest, setpointPage, setpointRequest } from './setpoints';
import { adminPage } from './admin';
import { welcomePage } from './welcome';
import { type AccessGroupEnv, inspectAccess, releaseSeat, setupAccess, syncAccessGroup } from './accessGroup';

interface Env extends AccessGroupEnv {
  DB: D1Database;
  ASSETS: Fetcher;
  ACCESS_TEAM_DOMAIN: string;
  ACCESS_AUD: string;
  LOCAL_UTC_OFFSET_MINUTES: string;
  // Hostname without Access, used in the public links (wrangler.toml).
  PUBLIC_ORIGIN?: string;
  // Bare domain that shows the welcome page (no Access), and the site it links to.
  WELCOME_HOST?: string;
  SITE_ORIGIN?: string;
  // Only in .dev.vars, and only honoured on localhost (cloud/README.md).
  DEV_USER_EMAIL?: string;
  // "dev" on the development Worker: reads every site, writes only to dev
  // sites (Povoto/docs/cloud-plan.md, "Ambientes e versões").
  CLOUD_ENV?: string;
  // Hostnames of the site pages behind Access, comma separated (Set up Access).
  ACCESS_HOSTS?: string;
}

const isDevCloud = (env: Env) => env.CLOUD_ENV === 'dev';

// Why this cloud may not write to the site (requests to the Povoto, batch
// deletion, ingest), or null when it may.
async function writeBlocked(env: Env, site: number): Promise<string | null> {
  if (!isDevCloud(env) || (await db.siteEnv(env.DB, site)) === 'dev') return null;
  return 'Production site: viewing only on the development cloud. Edit on povoto.brewtal.one.';
}

class Access {
  constructor(private readonly roles: Map<number, db.Role>, private readonly all: db.Role | null,
              readonly isAdmin: boolean) {}
  role(povotoId: number): db.Role | null {
    const own = this.roles.get(povotoId) ?? null;
    return own === 'edit' || this.all === 'edit' ? 'edit' : own ?? this.all;
  }
  canView(povotoId: number): boolean { return this.role(povotoId) !== null; }
  canEdit(povotoId: number): boolean { return this.role(povotoId) === 'edit'; }
  // null = every Povoto.
  viewableIds(): number[] | null { return this.all ? null : [...this.roles.keys()]; }
}

type App = { Bindings: Env; Variables: { email: string; access: Access } };
type Ctx = Context<App>;

const app = new Hono<App>();

// The bare domain (WELCOME_HOST) has no page of its own yet: it goes to the site.
app.use('*', async (c, next) => {
  setEnvironmentLabel(isDevCloud(c.env) ? 'DEV' : '');
  const url = new URL(c.req.url);
  if (!c.env.WELCOME_HOST || url.hostname !== c.env.WELCOME_HOST || url.pathname.startsWith('/assets/')) {
    return next();
  }
  return c.redirect(`${c.env.SITE_ORIGIN || url.origin}/`, 302);
});
const MAX_INGEST_BYTES = 256 * 1024;

const ASSET_TYPES: Record<string, string> = {
  'dashboard.html': 'text/html; charset=utf-8',
  'dashboard.js': 'application/javascript',
  'lcars.ttf': 'font/ttf',
  'LCars.bmp': 'image/bmp',
  'graphs.html': 'text/html; charset=utf-8',
  'graphs.js': 'application/javascript',
  'graphs.css': 'text/css',
  'uPlot.iife.min.js': 'application/javascript',
  'uPlot.min.css': 'text/css',
  'povoto.svg': 'image/svg+xml',
  'brewtal.svg': 'image/svg+xml',
  'povoto-light.svg': 'image/svg+xml',
  'brewtal-light.svg': 'image/svg+xml',
  'povoto-login.svg': 'image/svg+xml',
  'povoto-login-light.svg': 'image/svg+xml',
};
const DASHBOARD_FILES = new Set(['dashboard.js', 'lcars.ttf', 'LCars.bmp']);
const GRAPH_FILES = new Set(['graphs.js', 'graphs.css', 'uPlot.iife.min.js', 'uPlot.min.css']);
// Same paths as on the device (/assets/povoto.svg), public like the links.
const LOGO_FILES = new Set(['povoto.svg', 'brewtal.svg', 'povoto-light.svg', 'brewtal-light.svg', 'povoto-login.svg',
  'povoto-login-light.svg']);

function nowUtc(): number {
  return Math.floor(Date.now() / 1000);
}

function nowLocal(env: Env): number {
  return nowUtc() + Number(env.LOCAL_UTC_OFFSET_MINUTES || 0) * 60;
}

// Like the device, page files are revalidated on every load.
async function asset(c: Ctx, name: string): Promise<Response> {
  const response = await c.env.ASSETS.fetch(new URL(`/${name}`, c.req.url));
  if (!response.ok) return c.text('Not found', 404);
  // Through the context, so headers set before (the last-visit cookie) go too.
  return c.body(await response.arrayBuffer(), 200, { 'Content-Type': ASSET_TYPES[name], 'Cache-Control': 'no-cache' });
}

function noStore(response: Response): Response {
  response.headers.set('Cache-Control', 'no-store');
  return response;
}

function intParam(value: string | undefined): number | null {
  return value !== undefined && /^\d{1,9}$/.test(value) ? Number(value) : null;
}

// ---------------------------------------------------------------- SideKick

// The bearer token defines the site (scripts/add-sidekick.mjs).
async function tokenSidekick(c: Ctx): Promise<{ site: number; name: string; env: db.SiteEnv } | null> {
  const match = /^Bearer\s+(\S+)$/.exec(c.req.header('Authorization') ?? '');
  return match ? db.sidekickForTokenHash(c.env.DB, await sha256Hex(match[1])) : null;
}

app.post('/api/ingest', async c => {
  const sidekick = await tokenSidekick(c);
  if (sidekick === null) return c.json({ error: 'invalid token' }, 401);
  const site = sidekick.site;
  // 403: the SideKick keeps the lines and retries, nothing is lost.
  if (isDevCloud(c.env) && sidekick.env !== 'dev') {
    return c.json({ error: 'production site: post to the production cloud' }, 403);
  }
  const body = await c.req.text();
  // 400 makes the SideKick skip the post instead of retrying it forever.
  if (body.length > MAX_INGEST_BYTES) return c.json({ error: 'body too large' }, 400);
  const now = nowUtc();
  const { records, states, snapshots, acks, rejected } = parseBody(body, now);
  const inserted = await db.storeRecords(c.env.DB, site, records, states, now);
  if (rejected) console.log(`ingest site ${site}: ${rejected} invalid line(s)`);
  // Phase 2: store the copies and answers, ask for snapshots where the copy
  // is behind, and answer with the pending requests of this site's Povotos.
  await db.storePhase2(c.env.DB, site, snapshots, acks, states, now);
  await db.expireRequests(c.env.DB, site, now);
  await db.askStaleSnapshots(c.env.DB, [...new Set(states.map(s => site * 100 + s.num))], now);
  const requests = await db.takePendingRequests(c.env.DB, site, now);
  // NDJSON: a summary line, then one line per request. "p" comes right after
  // "k": the SideKick reads it to pick the Povoto.
  const lines = [JSON.stringify({ inserted, duplicates: records.length - inserted, states: states.length,
    snapshots: snapshots.length, acks: acks.length, rejected })];
  for (const r of requests) {
    lines.push(`{"k":"cmd","p":${r.povoto_id % 100},"id":${r.id},"t":${JSON.stringify(r.kind)},` +
      `"h":${JSON.stringify(r.base_hash ?? '')},"d":${r.data}}`);
  }
  return c.body(lines.join('\n') + '\n', 200, { 'Content-Type': 'application/x-ndjson' });
});

// The SideKick shows the site of its token on /getstatus.
app.get('/api/whoami', async c => {
  const sidekick = await tokenSidekick(c);
  if (sidekick === null) return c.json({ error: 'invalid token' }, 401);
  return c.json({ site: sidekick.site, name: sidekick.name });
});

// ------------------------------------------------- public link (no login)

app.get('/assets/:file', c => {
  const file = c.req.param('file');
  return LOGO_FILES.has(file) ? asset(c, file) : c.text('Not found', 404);
});

async function shareFor(c: Ctx): Promise<db.ShareRow | null> {
  const token = c.req.param('token') ?? '';
  return /^[A-Za-z0-9_-]{16,64}$/.test(token) ? db.getShare(c.env.DB, token) : null;
}

app.get('/s/:token', c => c.redirect(`/s/${c.req.param('token')}/`, 301));
app.get('/s/:token/', async c => (await shareFor(c)) ? asset(c, 'graphs.html')
  : c.html(messagePage('Link unavailable', 'This link was revoked or its batch was deleted.'), 404));
app.get('/s/:token/data.csv', async c => {
  const share = await shareFor(c);
  if (!share) return c.text('Not found', 404);
  const rows = await db.graphRows(c.env.DB, share.povoto_id, share.batch);
  return noStore(c.body(graphCsv(rows), 200, { 'Content-Type': 'text/csv; charset=utf-8' }));
});
app.get('/s/:token/meta.json', async c => {
  const share = await shareFor(c);
  if (!share) return c.json({ og: null, readOnly: true }, 404);
  const batch = await db.getBatch(c.env.DB, share.povoto_id, share.batch);
  return noStore(c.json({ og: batch?.og ?? null, readOnly: true }));
});
app.get('/s/:token/:file', async c => {
  const file = c.req.param('file');
  return GRAPH_FILES.has(file) && (await shareFor(c)) ? asset(c, file) : c.text('Not found', 404);
});

// --------------------------------------------------- everything else: login

function signedEmail(c: Ctx): Promise<string | null> {
  const host = new URL(c.req.url).hostname;
  const local = host === 'localhost' || host === '127.0.0.1';
  return local && c.env.DEV_USER_EMAIL ? Promise.resolve(c.env.DEV_USER_EMAIL.toLowerCase())
    : accessEmail(c.req.raw, c.env.ACCESS_TEAM_DOMAIN, c.env.ACCESS_AUD);
}

// The site opens where the last visit ended: the list, or the dashboard of
// the last Povoto seen (its dashboard or graphs). Kept per browser in a cookie.
const LAST_COOKIE = 'povoto_last';

function lastVisit(c: Ctx): string {
  const match = /^\/p\/(\d{1,9})\/dashboard\/$/.exec(getCookie(c, LAST_COOKIE) ?? '');
  return match ? match[0] : '/povotos';
}

// The root is outside Access (only /povotos, /search, /p and /admin are
// behind it): signed in, it goes to the last visit; otherwise the welcome
// page, whose Sign in leads there through the Access login.
app.get('/', async c => {
  if (!(await signedEmail(c))) return c.html(welcomePage(lastVisit(c), isDevCloud(c.env) ? 'DEV cloud' : ''));
  return c.redirect(lastVisit(c), 302);
});

app.use('*', async (c, next) => {
  const email = await signedEmail(c);
  if (!email) return c.html(messagePage('Access denied', 'Sign in through Cloudflare Access.'), 401);
  // povoto_id 0: every Povoto; site * 100 (100, 200...): every Povoto of that
  // site, including ones that appear later; otherwise one Povoto.
  const roles = new Map<number, db.Role>();
  const stronger = (id: number, role: db.Role) => { if (roles.get(id) !== 'edit') roles.set(id, role); };
  let all: db.Role | null = null;
  const siteRoles = new Map<number, db.Role>();
  for (const { povoto_id, role } of await db.permissionsFor(c.env.DB, email)) {
    if (povoto_id === 0) all = all === 'edit' ? all : role;
    else if (povoto_id % 100 === 0) siteRoles.set(povoto_id / 100, siteRoles.get(povoto_id / 100) === 'edit' ? 'edit' : role);
    else stronger(povoto_id, role);
  }
  if (siteRoles.size) {
    for (const { id, site } of await db.povotosOfSites(c.env.DB, [...siteRoles.keys()])) stronger(id, siteRoles.get(site)!);
  }
  // Administrator: edit on every site (all sites, or each one); manages the
  // permissions on /admin.
  const sites = [...(await db.siteNames(c.env.DB)).keys()];
  const isAdmin = all === 'edit' || (sites.length > 0 && sites.every(site => siteRoles.get(site) === 'edit'));
  c.set('email', email);
  c.set('access', new Access(roles, all, isAdmin));
  // Forms post only from these pages (no cross-site requests).
  if (c.req.method === 'POST' && c.req.header('Origin') !== new URL(c.req.url).origin) {
    return c.text('Forbidden', 403);
  }
  await next();
});

function rememberPage(c: Ctx, path: string): void {
  setCookie(c, LAST_COOKIE, path, { path: '/', maxAge: 365 * 86400, sameSite: 'Lax', secure: true, httpOnly: true });
}

// Site filter of the list (?site=N or ?site=all), kept per browser.
const SITE_COOKIE = 'povoto_site';

async function listPage(c: Ctx): Promise<Response> {
  rememberPage(c, '/povotos');
  const asked = c.req.query('site');
  const choice = asked ?? getCookie(c, SITE_COOKIE) ?? 'all';
  if (asked !== undefined && /^(all|\d{1,4})$/.test(asked)) {
    setCookie(c, SITE_COOKIE, asked, { path: '/', maxAge: 365 * 86400, sameSite: 'Lax', secure: true, httpOnly: true });
  }
  const selected = /^\d{1,4}$/.test(choice) ? Number(choice) : null;
  const [povotos, names] = await Promise.all([db.listPovotos(c.env.DB, c.get('access').viewableIds()),
    db.siteNames(c.env.DB)]);
  return c.html(homePage(c.get('email'), povotos, nowLocal(c.env), names, selected, c.get('access').isAdmin));
}

app.get('/povotos', listPage);

// --------------------------------------------- users (administrators only)

function adminBack(c: Ctx, message: string): Response {
  return c.redirect(`/admin?m=${encodeURIComponent(message)}`, 303);
}

app.get('/admin', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  const [permissions, sites, report] = await Promise.all([db.listPermissions(c.env.DB), db.siteNames(c.env.DB),
    inspectAccess(c.env)]);
  return noStore(c.html(adminPage(permissions, sites, c.get('email'), (c.req.query('m') ?? '').slice(0, 200), report)));
});

app.post('/admin/grant', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  const form = await c.req.parseBody();
  const email = typeof form.email === 'string' ? form.email.trim().toLowerCase() : '';
  const scope = intParam(typeof form.scope === 'string' ? form.scope : undefined);
  const role = form.role === 'edit' ? 'edit' : form.role === 'view' ? 'view' : null;
  const sites = await db.siteNames(c.env.DB);
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) || email.length > 120) return adminBack(c, 'Invalid e-mail.');
  if (scope === null || (scope !== 0 && (scope % 100 !== 0 || !sites.has(scope / 100))) || !role) {
    return adminBack(c, 'Choose a site and a role.');
  }
  if (email === c.get('email')) return adminBack(c, 'Your own access is not changed here.');
  await db.grantPermission(c.env.DB, email, scope, role);
  console.log(`permission ${email} ${scope} ${role} by ${c.get('email')}`);
  return adminBack(c, `Saved: ${email}.${await syncLogin(c)}`);
});

app.post('/admin/revoke', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  const form = await c.req.parseBody();
  const email = typeof form.email === 'string' ? form.email.trim().toLowerCase() : '';
  const scope = intParam(typeof form.scope === 'string' ? form.scope : undefined);
  if (!email || scope === null) return adminBack(c, 'Nothing removed.');
  if (email === c.get('email')) return adminBack(c, 'Your own access is not changed here.');
  await db.revokePermission(c.env.DB, email, scope);
  console.log(`permission ${email} ${scope} removed by ${c.get('email')}`);
  let seat = '';
  if (!(await db.listPermissions(c.env.DB)).some(p => p.email.toLowerCase() === email)) {
    // No access left: free the Zero Trust seat the person may hold.
    const problem = await releaseSeat(c.env, email);
    if (problem) console.log(`release seat ${email}: ${problem}`);
    seat = problem ? ` Seat NOT released: ${problem}` : ' Seat released.';
  }
  return adminBack(c, `Removed: ${email}.${await syncLogin(c)}${seat}`);
});

// Frees the seat of someone without access here.
app.post('/admin/release', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  const form = await c.req.parseBody();
  const email = typeof form.email === 'string' ? form.email.trim().toLowerCase() : '';
  if (!email || (await db.listPermissions(c.env.DB)).some(p => p.email.toLowerCase() === email)) {
    return adminBack(c, 'Only people without any access here can be released.');
  }
  const problem = await releaseSeat(c.env, email);
  return adminBack(c, problem ? `Seat NOT released: ${problem}` : `Seat of ${email} released.`);
});

// The whole Access setup in one step (policy, site application, public links).
app.post('/admin/setup', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  // Every cloud's hostname (production and development) in one application.
  const hostnames = (c.env.ACCESS_HOSTS || new URL(c.req.url).hostname).split(',').map(h => h.trim()).filter(Boolean);
  const emails = (await db.listPermissions(c.env.DB)).map(p => p.email);
  const auds = c.env.ACCESS_AUD.split(',').map(tag => tag.trim()).filter(Boolean);
  const steps = await setupAccess(c.env, hostnames, emails, auds);
  return adminBack(c, steps.join(' '));
});

app.post('/admin/sync', async c => {
  if (!c.get('access').isAdmin) return notFound(c);
  return adminBack(c, `Access policy checked.${await syncLogin(c)}`);
});

// Who may sign in (the Access policy) follows the e-mails with an access here.
async function syncLogin(c: Ctx): Promise<string> {
  const emails = (await db.listPermissions(c.env.DB)).map(p => p.email);
  const problem = await syncAccessGroup(c.env, emails);
  if (problem) console.log(`access policy sync: ${problem}`);
  return problem ? ` Sign-in list NOT updated: ${problem}` : ' Sign-in list up to date.';
}

app.get('/search', async c => {
  const query = (c.req.query('q') ?? '').trim().slice(0, 64);
  const access = c.get('access');
  const results = query ? await db.searchBatches(c.env.DB, query, access.viewableIds()) : [];
  const [povotos, sites] = await Promise.all([db.listPovotos(c.env.DB, access.viewableIds()),
    db.siteNames(c.env.DB)]);
  const manySites = new Set(povotos.map(p => p.site)).size > 1;
  const names = new Map(povotos.map(p => [p.id,
    manySites ? `${povotoName(p)} · ${sites.get(p.site) || `site ${p.site}`}` : povotoName(p)]));
  return c.html(searchPage(query, results, names));
});

// Resolves /p/:id for a user who may see it; null sends 404.
async function povotoFor(c: Ctx, edit = false) {
  const id = intParam(c.req.param('id'));
  const access = c.get('access');
  if (id === null || !(edit ? access.canEdit(id) : access.canView(id))) return null;
  return db.getPovoto(c.env.DB, id);
}

const notFound = (c: Ctx) => c.html(messagePage('Not found', 'No such Povoto or batch, or no permission.'), 404);

app.get('/p/:id', c => c.redirect(`/p/${c.req.param('id')}/`, 301));
app.get('/p/:id/', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return notFound(c);
  const canEdit = c.get('access').canEdit(povoto.id);
  const [batches, shares, latest] = await Promise.all([db.listBatches(c.env.DB, povoto.id),
    canEdit ? db.listShares(c.env.DB, povoto.id) : Promise.resolve([]),
    db.latestLog(c.env.DB, povoto.id)]);
  const publicOrigin = c.env.PUBLIC_ORIGIN || new URL(c.req.url).origin;
  return c.html(povotoPage(povoto, batches, shares, canEdit, publicOrigin,
    currentBatch(latest, nowLocal(c.env))));
});

// Dashboard: the device page, fed by the latest log row.
app.get('/p/:id/dashboard', c => c.redirect(`/p/${c.req.param('id')}/dashboard/`, 301));
app.get('/p/:id/dashboard/', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return notFound(c);
  rememberPage(c, `/p/${povoto.id}/dashboard/`);
  return asset(c, 'dashboard.html');
});
app.get('/p/:id/dashboard/status.json', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return c.json({}, 404);
  const log = await db.latestLog(c.env.DB, povoto.id);
  if (!log) return c.json({}, 404);
  const batch = await db.getBatch(c.env.DB, povoto.id, log.batch);
  const status = dashboardStatus(log, batch, povoto.num, nowLocal(c.env));
  if (isDevCloud(c.env)) status.back.label = 'Back · DEV cloud';
  return noStore(c.json(status));
});
app.get('/p/:id/dashboard/:file', async c => {
  const file = c.req.param('file');
  return DASHBOARD_FILES.has(file) && (await povotoFor(c)) ? asset(c, file) : notFound(c);
});

// Graphs: ?batch=N, or the latest batch.
async function graphBatch(c: Ctx, povotoId: number): Promise<db.BatchRow | null> {
  const requested = intParam(c.req.query('batch'));
  return requested === null ? db.latestBatch(c.env.DB, povotoId)
    : db.getBatch(c.env.DB, povotoId, requested);
}

app.get('/p/:id/graphs', c => c.redirect(`/p/${c.req.param('id')}/graphs/`, 301));
app.get('/p/:id/graphs/', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return notFound(c);
  rememberPage(c, `/p/${povoto.id}/dashboard/`);
  return asset(c, 'graphs.html');
});
app.get('/p/:id/graphs/data.csv', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return c.text('Not found', 404);
  const batch = await graphBatch(c, povoto.id);
  const rows = batch ? await db.graphRows(c.env.DB, povoto.id, batch.batch) : [];
  const name = batch ? `povoto-${povoto.id}-batch-${batch.batch}.csv` : 'povoto.csv';
  return noStore(c.body(graphCsv(rows), 200, {
    'Content-Type': 'text/csv; charset=utf-8',
    'Content-Disposition': `attachment; filename="${name}"`,
  }));
});
app.get('/p/:id/graphs/meta.json', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return c.json({ og: null }, 404);
  const [batch, batches] = await Promise.all([graphBatch(c, povoto.id), db.listBatches(c.env.DB, povoto.id)]);
  return noStore(c.json({
    og: batch?.og ?? null,
    batch: batch?.batch ?? null,
    batches: batches.map(b => ({ batch: b.batch, label: batchLabel(b) })),
  }));
});
app.get('/p/:id/graphs/:file', async c => {
  const file = c.req.param('file');
  return GRAPH_FILES.has(file) && (await povotoFor(c)) ? asset(c, file) : notFound(c);
});

// ---------------------------------------------- set points (phase 2)

app.get('/p/:id/setpoint', async c => {
  const povoto = await povotoFor(c);
  if (!povoto) return notFound(c);
  const now = nowUtc();
  const blocked = await writeBlocked(c.env, povoto.site);
  if (!blocked) await db.expireRequests(c.env.DB, povoto.site, now);
  const [sync, setpoints, rules, latest, requests, open] = await Promise.all([
    db.getSync(c.env.DB, povoto.id), db.getSetpointSnapshot(c.env.DB, povoto.id),
    db.getRuleSnapshots(c.env.DB, povoto.id), db.latestLog(c.env.DB, povoto.id),
    db.recentRequests(c.env.DB, povoto.id), db.openRequest(c.env.DB, povoto.id)]);
  return noStore(c.html(setpointPage({
    povoto, sync, setpoints, rules, latest, requests, open,
    canEdit: c.get('access').canEdit(povoto.id),
    writeBlocked: blocked,
    message: (c.req.query('m') ?? '').slice(0, 200),
    localOffsetSeconds: nowLocal(c.env) - now,
  })));
});

// Every request: edit role, edits on at the Povoto, nothing else waiting, and
// made on the copy the page showed (base). The Povoto checks the base again.
async function sendRequest(c: Ctx, kind: db.RequestKind,
                           build: (form: Record<string, unknown>) => Record<string, unknown> | string): Promise<Response> {
  const povoto = await povotoFor(c, true);
  if (!povoto) return notFound(c);
  const back = (message: string) => c.redirect(`/p/${povoto.id}/setpoint?m=${encodeURIComponent(message)}`, 303);
  const blocked = await writeBlocked(c.env, povoto.site);
  if (blocked) return back(blocked);
  const form = await c.req.parseBody();
  const now = nowUtc();
  await db.expireRequests(c.env.DB, povoto.site, now);
  const [sync, setpoints, rules, open] = await Promise.all([db.getSync(c.env.DB, povoto.id),
    db.getSetpointSnapshot(c.env.DB, povoto.id), db.getRuleSnapshots(c.env.DB, povoto.id),
    db.openRequest(c.env.DB, povoto.id)]);
  if (!sync || sync.edits_accepted !== 1) return back('Cloud edits are off on this Povoto.');
  if (open) return back('Another request is still waiting for the Povoto.');
  const current = kind === 'sp' ? setpoints?.hash
    : rules.length === 8 && rules.every(r => r.hash === sync.rules_hash) ? sync.rules_hash : null;
  if (!current || form.base !== current || (kind === 'sp' && current !== sync.setpoint_hash)) {
    return back('The Povoto changed since this page was loaded: check the values and try again.');
  }
  const data = build(form);
  if (typeof data === 'string') return back(data);
  await db.createRequest(c.env.DB, { povoto_id: povoto.id, kind, base_hash: current, data: JSON.stringify(data),
    created_by: c.get('email'), created_at: now });
  return back('Sent. The Povoto applies it within about a minute; reload to see the result.');
}

function ruleIndex(c: Ctx): number | null {
  const index = intParam(c.req.param('i'));
  return index !== null && index < 8 ? index : null;
}

app.post('/p/:id/setpoint', c => sendRequest(c, 'sp', setpointRequest));
app.post('/p/:id/setpoint/rule/:i', c => {
  const index = ruleIndex(c);
  return index === null ? notFound(c) : sendRequest(c, 'rule', form => ruleRequest(form, index));
});
app.post('/p/:id/setpoint/reset/:i', c => {
  const index = ruleIndex(c);
  return index === null ? notFound(c) : sendRequest(c, 'reset', () => ({ i: index }));
});
app.post('/p/:id/setpoint/trigger/:i', c => {
  const index = ruleIndex(c);
  return index === null ? notFound(c) : sendRequest(c, 'trigger', () => ({ i: index }));
});

// Editing: links and deletion ("edit" role).
app.post('/p/:id/batches/:batch/share', async c => {
  const povoto = await povotoFor(c, true);
  const batch = intParam(c.req.param('batch'));
  if (!povoto || batch === null || !(await db.getBatch(c.env.DB, povoto.id, batch))) return notFound(c);
  await db.createShare(c.env.DB, { token: randomToken(), povoto_id: povoto.id, batch,
    created_by: c.get('email'), created_at: nowUtc() });
  return c.redirect(`/p/${povoto.id}/`, 303);
});
app.post('/p/:id/shares/:token/revoke', async c => {
  const povoto = await povotoFor(c, true);
  if (!povoto) return notFound(c);
  await db.revokeShare(c.env.DB, povoto.id, c.req.param('token'), nowUtc());
  return c.redirect(`/p/${povoto.id}/`, 303);
});
app.post('/p/:id/batches/:batch/delete', async c => {
  const povoto = await povotoFor(c, true);
  const batch = intParam(c.req.param('batch'));
  if (!povoto || batch === null) return notFound(c);
  const blocked = await writeBlocked(c.env, povoto.site);
  if (blocked) return c.html(messagePage('Not deleted', blocked), 403);
  const form = await c.req.parseBody();
  if (form.confirm !== String(batch)) return c.text('Confirmation missing', 400);
  await db.deleteBatch(c.env.DB, povoto.id, batch);
  console.log(`batch ${batch} of Povoto ${povoto.id} deleted by ${c.get('email')}`);
  return c.redirect(`/p/${povoto.id}/`, 303);
});

app.notFound(c => c.html(messagePage('Not found', 'No such page.'), 404));

export default app;
