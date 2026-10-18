-- History records: the time between the last vents (Povoto/docs/cloud-log.md,
-- key "tblv", seconds) replaces the reliefs per hour. Batch state: "expansions"
-- becomes "reliefs" (key "rc"). Renames only: no site is in production.
ALTER TABLE logs RENAME COLUMN reliefs_per_hour TO tblv_seconds;
ALTER TABLE batches RENAME COLUMN expansions TO reliefs;
