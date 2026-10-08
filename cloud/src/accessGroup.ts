// Keeps the reusable Cloudflare Access policy that may sign in
// (ACCESS_POLICY_NAME, attached to the site's Access application) equal to the
// e-mails with a row in `permissions`, through the Cloudflare API, so only
// people given access on /admin can log in and take one of the Zero Trust
// seats. Rules of the policy that are not single e-mails are kept.

export interface AccessGroupEnv {
  // secret, permissions (Account): Access: Apps and Policies · Edit; Zero Trust · Edit
  CF_API_TOKEN?: string;
  CF_ACCOUNT_ID?: string;
  ACCESS_POLICY_NAME?: string;
}

interface Rule {
  email?: { email: string };
  [kind: string]: unknown;
}

interface Policy {
  id: string;
  name: string;
  decision: string;
  include: Rule[];
  exclude?: Rule[];
  require?: Rule[];
  session_duration?: string;
}

interface ApiResult<T> {
  success: boolean;
  result?: T;
  errors?: { message: string }[];
}

async function api<T>(env: AccessGroupEnv, path: string, init?: RequestInit): Promise<ApiResult<T>> {
  const response = await fetch(`https://api.cloudflare.com/client/v4/accounts/${env.CF_ACCOUNT_ID}${path}`, {
    ...init,
    headers: { Authorization: `Bearer ${env.CF_API_TOKEN}`, 'Content-Type': 'application/json' },
  });
  const body = await response.json<ApiResult<T>>().catch(() => ({ success: false } as ApiResult<T>));
  if (!body.success && !body.errors?.length) body.errors = [{ message: `HTTP ${response.status}` }];
  return body;
}

const apiError = (body: ApiResult<unknown>) => `Cloudflare API: ${body.errors?.map(e => e.message).join('; ')}`;

// The public links (/s/*) and their logos (/assets/*) open without login: a
// Bypass application for those paths of the site hostname. Created, or put
// back as expected when it exists. Returns null when done, or the problem.
export async function setupPublicLinks(env: AccessGroupEnv, hostname: string): Promise<string | null> {
  if (!env.CF_API_TOKEN || !env.CF_ACCOUNT_ID) return 'CF_API_TOKEN / CF_ACCOUNT_ID not configured.';
  const name = 'Povoto public links';
  const paths = [`${hostname}/s/*`, `${hostname}/assets/*`];
  const app = {
    name,
    type: 'self_hosted',
    domain: paths[0],
    destinations: paths.map(uri => ({ type: 'public', uri })),
    session_duration: '24h',
    app_launcher_visible: false,
    policies: [{ name: 'Everyone (public links)', decision: 'bypass', include: [{ everyone: {} }] }],
  };
  try {
    const listed = await api<{ id: string; name: string }[]>(env, '/access/apps');
    if (!listed.success) return apiError(listed);
    const existing = listed.result?.find(a => a.name === name);
    const saved = await api(env, existing ? `/access/apps/${existing.id}` : '/access/apps',
      { method: existing ? 'PUT' : 'POST', body: JSON.stringify(app) });
    return saved.success ? null : apiError(saved);
  } catch (error) {
    return `Cloudflare API unreachable: ${error instanceof Error ? error.message : String(error)}`;
  }
}

// Frees the Zero Trust seat of someone who no longer has any access.
export async function releaseSeat(env: AccessGroupEnv, email: string): Promise<string | null> {
  if (!env.CF_API_TOKEN || !env.CF_ACCOUNT_ID) return 'CF_API_TOKEN / CF_ACCOUNT_ID not configured.';
  try {
    const users = await api<{ email: string; seat_uid?: string; access_seat?: boolean }[]>(
      env, `/access/users?email=${encodeURIComponent(email)}`);
    if (!users.success) return apiError(users);
    const seats = (users.result ?? []).filter(u => u.email.toLowerCase() === email.toLowerCase() && u.seat_uid)
      .map(u => ({ seat_uid: u.seat_uid, access_seat: false, gateway_seat: false }));
    if (!seats.length) return null; // never signed in: no seat
    const saved = await api(env, '/access/seats', { method: 'PATCH', body: JSON.stringify(seats) });
    return saved.success ? null : apiError(saved);
  } catch (error) {
    return `Cloudflare API unreachable: ${error instanceof Error ? error.message : String(error)}`;
  }
}

// Returns null when the policy now has exactly these e-mails, or the problem.
export async function syncAccessGroup(env: AccessGroupEnv, emails: string[]): Promise<string | null> {
  if (!env.CF_API_TOKEN || !env.CF_ACCOUNT_ID || !env.ACCESS_POLICY_NAME) {
    return 'Access policy not configured (CF_API_TOKEN, CF_ACCOUNT_ID, ACCESS_POLICY_NAME).';
  }
  try {
    const listed = await api<Policy[]>(env, '/access/policies');
    if (!listed.success) return apiError(listed);
    const policy = listed.result?.find(p => p.name === env.ACCESS_POLICY_NAME);
    if (!policy) return `Access policy "${env.ACCESS_POLICY_NAME}" not found (Access controls > Policies).`;

    const wanted = [...new Set(emails.map(e => e.toLowerCase()))].sort();
    const current = policy.include.filter(r => r.email).map(r => r.email!.email.toLowerCase()).sort();
    if (wanted.join() === current.join()) return null;
    if (!wanted.length) return 'Refused to leave the Access policy without e-mails.';

    const include = [...policy.include.filter(r => !r.email), ...wanted.map(email => ({ email: { email } }))];
    const saved = await api(env, `/access/policies/${policy.id}`, {
      method: 'PUT',
      body: JSON.stringify({ name: policy.name, decision: policy.decision, include,
        exclude: policy.exclude ?? [], require: policy.require ?? [],
        ...(policy.session_duration ? { session_duration: policy.session_duration } : {}) }),
    });
    return saved.success ? null : apiError(saved);
  } catch (error) {
    return `Cloudflare API unreachable: ${error instanceof Error ? error.message : String(error)}`;
  }
}

interface App {
  id: string;
  name: string;
  domain?: string;
  self_hosted_domains?: string[];
  destinations?: { uri?: string }[];
  policies?: { id: string }[];
  [field: string]: unknown;
}

const SITE_APP_NAME = 'Povoto site';
// Every page route of the Worker that needs a login (src/index.ts).
const SITE_PATHS = ['/povotos', '/search', '/p/*', '/admin'];

// One step for the whole Access setup (Users page): the "Povoto users" policy
// with these e-mails, that policy as the only one of the site's application
// (the one that answers on `hostname`), and the public links exception.
export async function setupAccess(env: AccessGroupEnv, hostname: string, emails: string[],
                                  siteAuds: string[]): Promise<string[]> {
  if (!env.CF_API_TOKEN || !env.CF_ACCOUNT_ID || !env.ACCESS_POLICY_NAME) {
    return ['CF_API_TOKEN / CF_ACCOUNT_ID / ACCESS_POLICY_NAME not configured.'];
  }
  const wanted = [...new Set(emails.map(e => e.toLowerCase()))].sort();
  if (!wanted.length) return ['No e-mail with access yet.'];
  const done: string[] = [];
  try {
    // 1. The policy.
    const policies = await api<Policy[]>(env, '/access/policies');
    if (!policies.success) return [apiError(policies)];
    let policy = policies.result?.find(p => p.name === env.ACCESS_POLICY_NAME);
    if (!policy) {
      const created = await api<Policy>(env, '/access/policies', {
        method: 'POST',
        body: JSON.stringify({ name: env.ACCESS_POLICY_NAME, decision: 'allow',
          include: wanted.map(email => ({ email: { email } })) }),
      });
      if (!created.success || !created.result) return [`Policy: ${apiError(created)}`];
      policy = created.result;
      done.push(`Policy "${env.ACCESS_POLICY_NAME}" created.`);
    } else {
      const problem = await syncAccessGroup(env, wanted);
      if (problem) return [`Policy: ${problem}`];
      done.push(`Policy "${env.ACCESS_POLICY_NAME}" up to date.`);
    }

    // 2. Login by a code sent to the e-mail (One-time PIN).
    const providers = await api<{ id: string; type: string }[]>(env, '/access/identity_providers');
    if (!providers.success) return [...done, `Login methods: ${apiError(providers)}`];
    let pin = providers.result?.find(p => p.type === 'onetimepin');
    if (!pin) {
      const created = await api<{ id: string; type: string }>(env, '/access/identity_providers', {
        method: 'POST', body: JSON.stringify({ name: 'One-time PIN', type: 'onetimepin', config: {} }),
      });
      if (!created.success || !created.result) return [...done, `Login methods: ${apiError(created)}`];
      pin = created.result;
      done.push('One-time PIN login created.');
    }

    // 3. The site's own application for the hostname. The one made by
    // "Protect this Worker" always logs in with the Cloudflare account (e-mail
    // and password) whatever is set on it; a hostname application comes first
    // and lets the policy and the One-time PIN apply. Its AUD goes into
    // ACCESS_AUD (wrangler.toml), shown below.
    const apps = await api<App[]>(env, '/access/apps');
    if (!apps.success) return [...done, `Applications: ${apiError(apps)}`];
    // Only the site's pages: the root stays open for the welcome page (Access
    // cannot leave just "/" out, a path always covers its subpaths). Paths
    // outside these still get no identity from the Worker.
    const protectedPaths = SITE_PATHS.map(path => `${hostname}${path}`);
    const siteApp = {
      name: SITE_APP_NAME,
      type: 'self_hosted',
      domain: protectedPaths[0],
      destinations: protectedPaths.map(uri => ({ type: 'public', uri })),
      session_duration: '720h',
      app_launcher_visible: false,
      allowed_idps: [pin.id],
      auto_redirect_to_identity: true,
      policies: [{ id: policy.id, precedence: 1 }],
    };
    const existing = apps.result?.find(a => a.name === SITE_APP_NAME);
    const saved = await api<App>(env, existing ? `/access/apps/${existing.id}` : '/access/apps',
      { method: existing ? 'PUT' : 'POST', body: JSON.stringify(siteApp) });
    if (!saved.success || !saved.result) return [...done, `Application: ${apiError(saved)}`];
    const aud = typeof saved.result.aud === 'string' ? saved.result.aud : '';
    done.push(`Application "${SITE_APP_NAME}" ${existing ? 'updated' : 'created'} for ${hostname}` +
      ` (login by code sent to the e-mail)${siteAuds.includes(aud) ? '' : `; AUD ${aud} still to add to ACCESS_AUD`}.`);

    // 4. Public links.
    const links = await setupPublicLinks(env, hostname);
    done.push(links ? `Public links: ${links}` : 'Public links open without login.');
    return done;
  } catch (error) {
    return [...done, `Cloudflare API unreachable: ${error instanceof Error ? error.message : String(error)}`];
  }
}

export interface AccessReport {
  rules: string[];                                  // what the policy lets in
  seats: { email: string; lastLogin: string }[];    // Zero Trust users holding an Access seat
  problems: string[];
}

function describeRule(rule: Rule): string {
  if (rule.email) return rule.email.email;
  const [kind, value] = Object.entries(rule)[0] ?? ['?', ''];
  return `${kind}: ${JSON.stringify(value)}`;
}

// What the Users page shows about Cloudflare Access.
export async function inspectAccess(env: AccessGroupEnv): Promise<AccessReport> {
  const report: AccessReport = { rules: [], seats: [], problems: [] };
  if (!env.CF_API_TOKEN || !env.CF_ACCOUNT_ID || !env.ACCESS_POLICY_NAME) {
    report.problems.push('CF_API_TOKEN / CF_ACCOUNT_ID / ACCESS_POLICY_NAME not configured.');
    return report;
  }
  try {
    const policies = await api<Policy[]>(env, '/access/policies');
    const policy = policies.result?.find(p => p.name === env.ACCESS_POLICY_NAME);
    if (!policies.success) report.problems.push(`Policy: ${apiError(policies)}`);
    else if (!policy) report.problems.push(`Policy "${env.ACCESS_POLICY_NAME}" not found.`);
    else {
      report.rules = policy.include.map(describeRule);
      for (const rule of policy.require ?? []) report.rules.push(`required: ${describeRule(rule)}`);
    }
    const users = await api<{ email: string; access_seat?: boolean; last_successful_login?: string }[]>(
      env, '/access/users?per_page=100');
    if (!users.success) report.problems.push(`Seats: ${apiError(users)}`);
    else {
      report.seats = (users.result ?? []).filter(u => u.access_seat)
        .map(u => ({ email: u.email.toLowerCase(), lastLogin: (u.last_successful_login ?? '').slice(0, 16).replace('T', ' ') }));
    }
  } catch (error) {
    report.problems.push(`Cloudflare API unreachable: ${error instanceof Error ? error.message : String(error)}`);
  }
  return report;
}
