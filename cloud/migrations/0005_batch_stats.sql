-- Dashboard statistics (Povoto/docs/web-dashboard.md): temperature mode and
-- headspace volume from the batch state line ("tm", "hv"). NULL = firmware
-- that does not send them.
ALTER TABLE batches ADD COLUMN temp_mode TEXT;          -- 'chill', 'heat', 'idle'
ALTER TABLE batches ADD COLUMN headspace_volume REAL;   -- L
