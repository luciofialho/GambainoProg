// Welcome page at the root of the site, outside Access, for visitors not
// signed in: the Povoto logo, a Sign in button to a protected page (where
// Cloudflare Access asks for the e-mail) and the Brewtal signature.
import { escapeHtml } from './pages';

export function welcomePage(signInPath: string): string {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Povoto · Brewtal</title><link rel="icon" href="/assets/povoto.svg">
<style>
:root { color-scheme: dark; font-family: Arial, sans-serif; background: #16191e; color: #e8edf3; }
body { margin: 0; min-height: 100vh; display: flex; flex-direction: column; align-items: center;
  justify-content: center; gap: 28px; padding: 24px; box-sizing: border-box; text-align: center; }
.logo { width: min(240px, 60vw); height: auto; }
h1 { font-size: 34px; letter-spacing: .5px; margin: 0; }
p { margin: 0; color: #aeb9c6; }
.button { display: inline-block; border: 1px solid #6e91dd; border-radius: 9px; background: #435e9a;
  color: #f2f5fa; padding: 12px 34px; font-size: 18px; text-decoration: none; }
.button:hover { background: #4f6db0; }
.signature { margin-top: 24px; width: min(180px, 45vw); height: auto; opacity: .75; }
</style></head>
<body>
<img class="logo" src="/assets/povoto-login-light.svg" alt="Povoto">
<h1>Povoto</h1>
<a class="button" href="${escapeHtml(signInPath)}">Sign in</a>
<img class="signature" src="/assets/brewtal-light.svg" alt="Brewtal">
</body></html>`;
}
