-- Povoto cloud, phase 1 (Povoto/docs/cloud-plan.md).
-- Povoto id = site * 100 + PovotoNum. Epochs are the Povoto's local epoch
-- (UTC-3 wall clock), as in its own CSV.

-- One row per SideKick (site). The token itself is never stored.
CREATE TABLE sidekicks (
  site INTEGER PRIMARY KEY,
  name TEXT NOT NULL DEFAULT '',
  token_hash TEXT NOT NULL UNIQUE -- SHA-256 hex of the bearer token
);

CREATE TABLE povotos (
  id INTEGER PRIMARY KEY,
  site INTEGER NOT NULL,
  num INTEGER NOT NULL
);

CREATE TABLE batches (
  povoto_id INTEGER NOT NULL,
  batch INTEGER NOT NULL,
  name TEXT NOT NULL DEFAULT '',
  date TEXT NOT NULL DEFAULT '',
  og REAL,
  first_epoch INTEGER NOT NULL,
  last_epoch INTEGER NOT NULL,
  PRIMARY KEY (povoto_id, batch)
);
CREATE INDEX batches_number ON batches (batch);

-- One row per Povoto per slot; a repeated slot is ignored on insert.
CREATE TABLE logs (
  povoto_id INTEGER NOT NULL,
  epoch INTEGER NOT NULL,
  batch INTEGER NOT NULL,
  mode INTEGER NOT NULL,
  temp REAL,
  temp_sp REAL,
  temp_slow REAL,
  press REAL,
  press_sp REAL,
  press_slow REAL,
  sg REAL,
  abv REAL,
  rate REAL,
  flags INTEGER NOT NULL DEFAULT 0,
  volume REAL,
  co2_mass REAL,
  reliefs_per_hour REAL,
  synthetic_start INTEGER, -- day 0 of the synthetic profile (debug benches only)
  received_at INTEGER NOT NULL, -- UTC epoch when the cloud stored it
  PRIMARY KEY (povoto_id, epoch)
) WITHOUT ROWID;
CREATE INDEX logs_batch ON logs (povoto_id, batch, epoch);

-- role: 'view' or 'edit'. povoto_id 0 = every Povoto.
CREATE TABLE permissions (
  email TEXT NOT NULL,
  povoto_id INTEGER NOT NULL,
  role TEXT NOT NULL CHECK (role IN ('view', 'edit')),
  PRIMARY KEY (email, povoto_id)
);

-- Public read-only links to one batch's graphs.
CREATE TABLE shares (
  token TEXT PRIMARY KEY,
  povoto_id INTEGER NOT NULL,
  batch INTEGER NOT NULL,
  created_by TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  revoked_at INTEGER
);
CREATE INDEX shares_batch ON shares (povoto_id, batch);
