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
5. Cloudflare Access: the "Set up Access" button on the Users page creates
   the "Povoto site" application (site paths only, One-time PIN, policy
   "Povoto users"); its AUD and the team domain
   (`https://brewtal.cloudflareaccess.com`) go into `ACCESS_AUD` and
   `ACCESS_TEAM_DOMAIN` of both environments in `wrangler.toml`. Do not use
   "Protect this Worker behind Access" (Workers & Pages → tab Access): that
   application covers every hostname of the Worker, root included, and logs
   in only with the Cloudflare account. povoto-cloud has no workers.dev
   address (`workers_dev = false`).
6. One token per SideKick site: `node scripts/add-sidekick.mjs <site> [name]`.
   The token is printed once; type it on the SideKick page "Connection
   settings" (link on `/getstatus`) with the URL
   `https://povoto-public.povoto-cloud.workers.dev/api/ingest`. The SideKick
   then shows its site on `/getstatus` (asked to `/api/whoami`).
7. Permissions, on the Users page (`/admin`, link at the right of the site
   buttons): per site or every site, role `view` or `edit`. Administrators
   are the users with `edit` on every site. Rows in `permissions`: povoto_id
   0 = every site, site × 100 = one site (and its future Povotos), site × 100
   + PovotoNum = one Povoto (still honoured, not offered by the page). The
   first administrator needs SQL:

   ```
   npx wrangler d1 execute povoto --remote --command "INSERT INTO permissions VALUES ('someone@example.com', 0, 'edit')"
   ```

   Access only proves the e-mail: its policy lets anyone with a verified
   e-mail in (one-time PIN / Google), and the Worker shows nothing without
   a row here. Public links (`/s/*`, `/assets/*`) have a Bypass application
   and count as no user.

## Day to day

- Automatic deploy (Cloudflare Workers Builds, connected to GitHub on
  2026-10-05): a push to `master` that touches `cloud/`, `Povoto/data/` or
  `Povoto/include/PovotoLogos.h` runs `npm run assets` in `cloud/` and then
  one deploy per Worker, each from its own connected build: povoto-cloud
  runs `npx wrangler deploy`, povoto-public runs
  `npx wrangler deploy --env public`. A connected build deploys only its own
  Worker: with both commands in one build, the second overrode the name and
  deployed the public config onto povoto-cloud. Close without merging any
  Cloudflare pull request that renames the Worker in `wrangler.toml`.
  Results in the dashboard: Workers & Pages → povoto-cloud → Deployments.
  Database migrations are not applied by it: run `npm run migrate:remote`.
  The build clones the whole repository: a nested git repository committed
  as a submodule without `.gitmodules` (the old `Gambaino/` Lovable folder)
  made it fail at "updating repository submodules". Retry rebuilds the same
  commit; a new push to a watched path builds the latest one.
- By hand, from this folder: `npm run deploy`.
- Order when the database or the record format changes: run
  `npm run migrate:remote` before pushing (the push deploys code that may
  need the new columns), then update the SideKicks and only then the
  Povotos. The Worker keeps accepting the older record formats, so a site
  can run old firmware for a while. Batch state lines (`"k":"s"`,
  migration 0002) need the new SideKick to reach the cloud at all.
  Phase 2 (set points and rules, migration 0003): the ingest answer became
  NDJSON (summary line, then requests); old SideKicks ignore it.
- Set points page: `/p/<id>/setpoint` (also the set point touch zones of the
  dashboard). Edits need the `edit` role, "Accept cloud edits" on at the
  Povoto and an up-to-date copy (Povoto/docs/cloud-log.md, "Fase 2").
- Addresses: `povoto.brewtal.one` (the site), `public.brewtal.one` (ingest
  and public links) and `brewtal.one` (for now a redirect to the site,
  `WELCOME_HOST`). A custom domain only attaches when the hostname has no DNS
  records of its own.
- Access covers only `/povotos`, `/search`, `/p/*` and `/admin` (the "Set up
  Access" button on the Users page): the root `/` stays open for the welcome
  page (`src/welcome.ts`). Access cannot leave just `/` out, since a path
  always covers its subpaths. The Worker still refuses any page without a
  valid Access token (header, or the `CF_Authorization` cookie at the root).
- The site root, signed in, opens where the browser last was: the Povoto list
  (`/povotos`) or the dashboard of the last Povoto seen (cookie
  `povoto_last`, set by the list, dashboard and graphs pages). Not signed
  in, it shows the welcome page, whose Sign in goes to that same page.
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
