// Copies the Povoto web pages used by the cloud into .build/assets, so the
// pages exist only once in the repository (Povoto/data/www).
import { gunzipSync } from 'node:zlib';
import { copyFileSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const www = join(root, '..', 'Povoto', 'data', 'www');
const out = join(root, '.build', 'assets');

// Files are overwritten, not the folder removed: a synced folder (A:\Nuvem)
// may refuse to delete a directory.
const pages = ['dashboard.html', 'dashboard.js', 'graphs.html', 'graphs.js', 'graphs.css',
  'uPlot.iife.min.js', 'uPlot.min.css', 'uPlot.LICENSE'];
mkdirSync(out, { recursive: true });
for (const name of readdirSync(out)) {
  if (!pages.includes(name) && !['LCars.bmp', 'lcars.ttf', 'povoto.svg', 'brewtal.svg'].includes(name)) {
    rmSync(join(out, name), { recursive: true, force: true });
  }
}
for (const name of pages) copyFileSync(join(www, name), join(out, name));
copyFileSync(join(www, '..', 'LCars.bmp'), join(out, 'LCars.bmp'));
// The logos live in the firmware (PovotoLogos.h, raw string literals).
const logos = readFileSync(join(www, '..', '..', 'include', 'PovotoLogos.h'), 'utf8');
for (const [tag, name] of [['POVOTO', 'povoto.svg'], ['BREWTAL', 'brewtal.svg']]) {
  const match = new RegExp(`R"${tag}\\(([\\s\\S]*?)\\)${tag}"`).exec(logos);
  if (!match) throw new Error(`${tag} logo not found in PovotoLogos.h`);
  writeFileSync(join(out, name), match[1]);
}
// The device stores the font gzipped; Cloudflare compresses responses itself.
writeFileSync(join(out, 'lcars.ttf'), gunzipSync(readFileSync(join(www, 'lcars.ttf.gz'))));
console.log(`Assets copied to ${out}`);
