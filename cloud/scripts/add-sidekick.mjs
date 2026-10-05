// Creates (or replaces) the token of one SideKick site and prints it once.
//   node scripts/add-sidekick.mjs <site> [name] [--local]
// The database keeps only the token's SHA-256; type the token on the
// SideKick's /cloud page.
import { createHash, randomBytes } from 'node:crypto';
import { execSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = process.argv.slice(2);
const local = args.includes('--local');
const [siteText, name = ''] = args.filter(arg => arg !== '--local');
const site = Number(siteText);
if (!Number.isInteger(site) || site < 1 || site > 9999) {
  console.error('Usage: node scripts/add-sidekick.mjs <site> [name] [--local]');
  process.exit(1);
}
const token = randomBytes(24).toString('base64url');
const hash = createHash('sha256').update(token).digest('hex');
const sql = `INSERT INTO sidekicks (site, name, token_hash) VALUES (${site}, '${name.replace(/'/g, "''")}', '${hash}')
  ON CONFLICT (site) DO UPDATE SET name = excluded.name, token_hash = excluded.token_hash;`;
// Through a file: the shell would split a --command argument.
const dir = mkdtempSync(join(tmpdir(), 'povoto-'));
const file = join(dir, 'sidekick.sql');
writeFileSync(file, sql);
try {
  execSync(`npx wrangler d1 execute povoto ${local ? '--local' : '--remote'} --file "${file}"`, { stdio: 'inherit' });
} finally {
  rmSync(dir, { recursive: true, force: true });
}
console.log(`\nSite ${site} token (shown only now): ${token}`);
