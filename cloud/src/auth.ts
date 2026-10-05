// Cloudflare Access: the user's e-mail comes from the signed JWT that Access
// adds to every request it lets through (Cf-Access-Jwt-Assertion). The token
// is verified here too, so a request that reaches the Worker without passing
// Access (another hostname, a misconfigured policy) gets no identity.

interface Jwk extends JsonWebKey {
  kid: string;
}

let certsCache: { url: string; keys: Jwk[]; fetchedAt: number } | null = null;
const CERTS_TTL_MS = 60 * 60 * 1000;

function base64UrlDecode(value: string): Uint8Array {
  const base64 = value.replace(/-/g, '+').replace(/_/g, '/');
  const binary = atob(base64 + '='.repeat((4 - (base64.length % 4)) % 4));
  return Uint8Array.from(binary, char => char.charCodeAt(0));
}

function decodeJson(part: string): Record<string, unknown> | null {
  try {
    return JSON.parse(new TextDecoder().decode(base64UrlDecode(part)));
  } catch {
    return null;
  }
}

async function accessKeys(teamDomain: string, refresh: boolean): Promise<Jwk[]> {
  const url = `${teamDomain.replace(/\/$/, '')}/cdn-cgi/access/certs`;
  if (!refresh && certsCache && certsCache.url === url &&
      Date.now() - certsCache.fetchedAt < CERTS_TTL_MS) {
    return certsCache.keys;
  }
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Access certs: HTTP ${response.status}`);
  const { keys } = await response.json<{ keys: Jwk[] }>();
  certsCache = { url, keys, fetchedAt: Date.now() };
  return keys;
}

// Returns the authenticated e-mail, or null.
export async function accessEmail(request: Request, teamDomain: string, aud: string): Promise<string | null> {
  const token = request.headers.get('Cf-Access-Jwt-Assertion');
  if (!token || !teamDomain || !aud) return null;
  const parts = token.split('.');
  if (parts.length !== 3) return null;
  const header = decodeJson(parts[0]);
  const payload = decodeJson(parts[1]);
  if (!header || !payload || header.alg !== 'RS256' || typeof header.kid !== 'string') return null;

  let keys = await accessKeys(teamDomain, false);
  let jwk = keys.find(key => key.kid === header.kid);
  if (!jwk) { // keys rotate: one refresh before refusing
    keys = await accessKeys(teamDomain, true);
    jwk = keys.find(key => key.kid === header.kid);
  }
  if (!jwk) return null;
  const key = await crypto.subtle.importKey('jwk', jwk,
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify']);
  const valid = await crypto.subtle.verify('RSASSA-PKCS1-v1_5', key, base64UrlDecode(parts[2]),
    new TextEncoder().encode(`${parts[0]}.${parts[1]}`));
  if (!valid) return null;

  const now = Date.now() / 1000;
  const audiences = Array.isArray(payload.aud) ? payload.aud : [payload.aud];
  if (!audiences.includes(aud)) return null;
  if (typeof payload.exp !== 'number' || payload.exp < now) return null;
  if (typeof payload.nbf === 'number' && payload.nbf > now + 60) return null;
  if (payload.iss !== teamDomain.replace(/\/$/, '')) return null;
  return typeof payload.email === 'string' ? payload.email.toLowerCase() : null;
}

export async function sha256Hex(value: string): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value));
  return [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

// 128 random bits, URL-safe.
export function randomToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
