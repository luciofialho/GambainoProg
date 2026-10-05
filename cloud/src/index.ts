// Povoto cloud, phase 1 (Povoto/docs/cloud-plan.md, cloud/README.md).
import { Hono, type Context } from 'hono';
import { getCookie, setCookie } from 'hono/cookie';
import { accessEmail, randomToken, sha256Hex } from './auth';
import * as db from './db';
import { batchLabel, dashboardStatus, graphCsv } from './format';
import { homePage, messagePage, povotoName, povotoPage, searchPage } from './pages';
import { parseBody } from './records';

interface Env {
  DB: D1Database;
  ASSETS: Fetcher;
  ACCESS_TEAM_DOMAIN: string;
  ACCESS_AUD: string;
  LOCAL_UTC_OFFSET_MINUTES: string;
  // Hostname without Access, used in the public links (wrangler.toml).
  PUBLIC_ORIGIN?: string;
  // Only in .dev.vars, and only honoured on localhost (cloud/README.md).
  DEV_USER_EMAIL?: string;
}

class Access {
  constructor(private readonly roles: Map<number, db.Role>, private readonly all: db.Role | null) {}
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
};
const DASHBOARD_FILES = new Set(['dashboard.js', 'lcars.ttf', 'LCars.bmp']);
const GRAPH_FILES = new Set(['graphs.js', 'graphs.css', 'uPlot.iife.min.js', 'uPlot.min.css']);
// Same paths as on the device (/assets/povoto.svg), public like the links.
const LOGO_FILES = new Set(['povoto.svg', 'brewtal.svg']);

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

app.post('/api/ingest', async c => {
  const match = /^Bearer\s+(\S+)$/.exec(c.req.header('Authorization') ?? '');
  const site = match ? await db.siteForTokenHash(c.env.DB, await sha256Hex(match[1])) : null;
  if (site === null) return c.json({ error: 'invalid token' }, 401);
  const body = await c.req.text();
  // 400 makes the SideKick skip the post instead of retrying it forever.
  if (body.length > MAX_INGEST_BYTES) return c.json({ error: 'body too large' }, 400);
  const { records, rejected } = parseBody(body, nowUtc());
  const inserted = await db.storeRecords(c.env.DB, site, records, nowUtc());
  if (rejected) console.log(`ingest site ${site}: ${rejected} invalid line(s)`);
  return c.json({ inserted, duplicates: records.length - inserted, rejected });
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

app.use('*', async (c, next) => {
  const host = new URL(c.req.url).hostname;
  const local = host === 'localhost' || host === '127.0.0.1';
  const email = local && c.env.DEV_USER_EMAIL ? c.env.DEV_USER_EMAIL.toLowerCase()
    : await accessEmail(c.req.raw, c.env.ACCESS_TEAM_DOMAIN, c.env.ACCESS_AUD);
  if (!email) return c.html(messagePage('Access denied', 'Sign in through Cloudflare Access.'), 401);
  const roles = new Map<number, db.Role>();
  let all: db.Role | null = null;
  for (const { povoto_id, role } of await db.permissionsFor(c.env.DB, email)) {
    if (povoto_id === 0) all = all === 'edit' ? all : role;
    else roles.set(povoto_id, role);
  }
  c.set('email', email);
  c.set('access', new Access(roles, all));
  // Forms post only from these pages (no cross-site requests).
  if (c.req.method === 'POST' && c.req.header('Origin') !== new URL(c.req.url).origin) {
    return c.text('Forbidden', 403);
  }
  await next();
});

// The site opens where the last visit ended: the list, or the dashboard of
// the last Povoto seen (its dashboard or graphs). Kept per browser in a cookie.
const LAST_COOKIE = 'povoto_last';

function rememberPage(c: Ctx, path: string): void {
  setCookie(c, LAST_COOKIE, path, { path: '/', maxAge: 365 * 86400, sameSite: 'Lax', secure: true, httpOnly: true });
}

async function listPage(c: Ctx): Promise<Response> {
  rememberPage(c, '/povotos');
  const povotos = await db.listPovotos(c.env.DB, c.get('access').viewableIds());
  return c.html(homePage(c.get('email'), povotos, nowLocal(c.env)));
}

app.get('/', async c => {
  const match = /^\/p\/(\d{1,9})\/dashboard\/$/.exec(getCookie(c, LAST_COOKIE) ?? '');
  if (match && c.get('access').canView(Number(match[1]))) return c.redirect(match[0], 302);
  return listPage(c);
});
app.get('/povotos', listPage);

app.get('/search', async c => {
  const query = (c.req.query('q') ?? '').trim().slice(0, 64);
  const access = c.get('access');
  const results = query ? await db.searchBatches(c.env.DB, query, access.viewableIds()) : [];
  const names = new Map((await db.listPovotos(c.env.DB, access.viewableIds()))
    .map(p => [p.id, povotoName(p)]));
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
  const [batches, shares] = await Promise.all([db.listBatches(c.env.DB, povoto.id),
    canEdit ? db.listShares(c.env.DB, povoto.id) : Promise.resolve([])]);
  const publicOrigin = c.env.PUBLIC_ORIGIN || new URL(c.req.url).origin;
  return c.html(povotoPage(povoto, batches, shares, canEdit, publicOrigin));
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
  return noStore(c.json(dashboardStatus(log, batch, povoto.num, nowLocal(c.env))));
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
  const form = await c.req.parseBody();
  if (form.confirm !== String(batch)) return c.text('Confirmation missing', 400);
  await db.deleteBatch(c.env.DB, povoto.id, batch);
  console.log(`batch ${batch} of Povoto ${povoto.id} deleted by ${c.get('email')}`);
  return c.redirect(`/p/${povoto.id}/`, 303);
});

app.notFound(c => c.html(messagePage('Not found', 'No such page.'), 404));

export default app;
