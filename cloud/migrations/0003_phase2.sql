-- Phase 2 (Povoto/docs/cloud-log.md): the cloud copy of each Povoto's set
-- points and automatic rules (latest snapshot only, sent by the Povoto) and
-- the requests made on the site. The Povoto owns the data: a request applies
-- only when made on the version (hash) the Povoto still has.

CREATE TABLE setpoint_snapshots (
  povoto_id INTEGER PRIMARY KEY,
  hash TEXT NOT NULL,
  data TEXT NOT NULL,        -- JSON object as the Povoto sent it
  epoch INTEGER NOT NULL     -- local epoch on the Povoto when sent
);

CREATE TABLE rule_snapshots (
  povoto_id INTEGER NOT NULL,
  idx INTEGER NOT NULL,      -- 0..7
  hash TEXT NOT NULL,        -- hash of the 8 rules when this one was sent
  data TEXT NOT NULL,
  epoch INTEGER NOT NULL,
  PRIMARY KEY (povoto_id, idx)
);

-- What the Povoto reported in its latest batch state line.
CREATE TABLE povoto_sync (
  povoto_id INTEGER PRIMARY KEY,
  setpoint_hash TEXT,
  rules_hash TEXT,
  edits_accepted INTEGER,
  epoch INTEGER NOT NULL
);

CREATE TABLE requests (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  povoto_id INTEGER NOT NULL,
  kind TEXT NOT NULL,              -- sp, rule, reset, trigger, snap
  base_hash TEXT,
  data TEXT NOT NULL,              -- JSON object
  created_by TEXT NOT NULL,
  created_at INTEGER NOT NULL,     -- UTC epoch
  sent_at INTEGER,                 -- given to the SideKick
  -- pending, sent, applied, rejected, expired (never sent), unconfirmed (sent, no answer)
  status TEXT NOT NULL DEFAULT 'pending',
  message TEXT,
  done_at INTEGER
);
CREATE INDEX requests_povoto ON requests (povoto_id, status);
