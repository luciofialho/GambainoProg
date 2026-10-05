# Povoto cloud

Cloudflare Worker + D1 that receives the Povoto log from the SideKicks and
serves the Povoto `/dashboard` and `/graphs` pages remotely. Plan and
decisions: `Povoto/docs/cloud-plan.md`; record format and the device side:
`Povoto/docs/cloud-log.md`.

- `src/index.ts`: routes (Hono). `src/db.ts`: all SQL (SQLite). `src/auth.ts`:
  Cloudflare Access JWT. `src/format.ts`: the device's JSON/CSV formats.
  `src/pages.ts`: the cloud's own pages.
- `migrations/`: D1 schema.
- The Povoto pages are not copied into the repository: `npm run assets`
  copies `Povoto/data/www` and `LCars.bmp` into `.build/assets` (ignored) before
  `dev` and `deploy`.

## First setup

1. `npm install`, then `npx wrangler login`.
2. `npx wrangler d1 create povoto` and copy the `database_id` into
   `wrangler.toml`.
3. `npm run migrate:remote`.
4. `npm run deploy`. It publishes the same code twice (`wrangler.toml`):
   - `povoto-cloud` (https://povoto-cloud.povoto-cloud.workers.dev): the site,
     behind Cloudflare Access;
   - `povoto-public` (https://povoto-public.povoto-cloud.workers.dev): no
     Access; only `/api/ingest` (SideKick token) and the public links `/s/...`
     work there, every other page answers 401 because the code itself
     requires the Access login. Public links are built with `PUBLIC_ORIGIN`.
5. Cloudflare Access, done once in the dashboard: Workers & Pages →
   povoto-cloud → tab **Access** → "Protect this Worker behind Access", scope
   **All traffic**, policy **Cloudflare account** (only the account owner).
   Copy the AUD tag and the team domain (from the JWKS URL,
   `https://brewtal.cloudflareaccess.com`) into `ACCESS_AUD` and
   `ACCESS_TEAM_DOMAIN` of both environments in `wrangler.toml`, then deploy.
   Without them nobody gets in: the Worker verifies the Access token on every
   request. To let other people in, the Access application needs a policy by
   e-mail (Zero Trust → Access → Applications → the povoto-cloud app →
   Policies), besides their row in `permissions`.
6. One token per SideKick site: `node scripts/add-sidekick.mjs <site> [name]`.
   The token is printed once; type it on the SideKick page `/cloud` with the
   URL `https://povoto-public.povoto-cloud.workers.dev/api/ingest`.
7. Permissions (role `view` or `edit`; Povoto id = site × 100 + PovotoNum;
   id 0 = every Povoto):

   ```
   npx wrangler d1 execute povoto --remote --command "INSERT INTO permissions VALUES ('someone@example.com', 103, 'view')"
   ```

## Day to day

- Automatic deploy (Cloudflare Workers Builds, connected to GitHub on
  2026-10-05): a push to `master` that touches `cloud/`, `Povoto/data/` or
  `Povoto/include/PovotoLogos.h` runs `npm run assets` and
  `npx wrangler deploy && npx wrangler deploy --env public` in `cloud/`.
  Results in the dashboard: Workers & Pages → povoto-cloud → Deployments.
  Database migrations are not applied by it: run `npm run migrate:remote`.
- By hand, from this folder: `npm run deploy`.
- Phase 1 check: `node scripts/check-phase1.mjs <povotoId> [batch]` (rows per
  day, gaps, delay, synthetic values).
- Without hardware: `node scripts/feed-test.mjs https://povoto-public.povoto-cloud.workers.dev/api/ingest <token> [num] [batch] [days]`
  posts synthetic records like a SideKick. Delete the batch afterwards on the
  Povoto page.

## Local development

`.dev.vars` with `DEV_USER_EMAIL=dev@localhost` (honoured only on
localhost), then `npm run migrate:local`, add a SideKick with `--local`, a
permission for `dev@localhost`, and `npm run dev`.

## Free plan budget

Per day: ~288 posts per Povoto at most (the SideKick groups records), a few
requests per page view, rows read ≈ rows shown (one graph of a 14-day batch
reads ~4,000 rows). Limits: 100,000 Worker requests and 5 million D1 rows
read per day.
