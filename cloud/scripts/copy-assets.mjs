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
  if (!pages.includes(name) && !['LCars.bmp', 'lcars.ttf', 'povoto.svg', 'brewtal.svg', 'povoto-light.svg', 'brewtal-light.svg', 'povoto-login.svg', 'povoto-login-light.svg'].includes(name)) {
    rmSync(join(out, name), { recursive: true, force: true });
  }
}
for (const name of pages) copyFileSync(join(www, name), join(out, name));
copyFileSync(join(www, '..', 'LCars.bmp'), join(out, 'LCars.bmp'));
// The logos, as the Povoto serves them (Povoto/data/www/assets).
for (const name of ['povoto.svg', 'brewtal.svg']) {
  const logo = readFileSync(join(www, 'assets', name), 'utf8');
  writeFileSync(join(out, name), logo);
  // Light version for dark backgrounds (Access login page): the logos are black.
  writeFileSync(join(out, name.replace('.svg', '-light.svg')), logo.replace(/#000000/gi, '#e8edf3'));
}
// Access login page: the logo box is small and fixed, so the Povoto logo goes
// cropped to its drawing (measured bounds 407,239 629x639 of the 1200 frame).
const povoto = readFileSync(join(out, 'povoto.svg'), 'utf8');
const cropped = povoto
  .replace('viewBox="0 0 1200 1200"', 'viewBox="392 228 660 660"')
  .replace('width="1200"', 'width="660"').replace('height="1200"', 'height="660"');
writeFileSync(join(out, 'povoto-login.svg'), cropped);
// Same crop, light: the welcome page.
writeFileSync(join(out, 'povoto-login-light.svg'), cropped.replace(/#000000/gi, '#e8edf3'));
// The device stores the font gzipped; Cloudflare compresses responses itself.
writeFileSync(join(out, 'lcars.ttf'), gunzipSync(readFileSync(join(www, 'lcars.ttf.gz'))));
console.log(`Assets copied to ${out}`);
