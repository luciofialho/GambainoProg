-- Batch state (Povoto/docs/cloud-log.md): only the latest version per batch,
-- no history. Sent by the Povoto every slot outside the SideKick spool.
ALTER TABLE batches ADD COLUMN state_epoch INTEGER;   -- local epoch of the stored state
ALTER TABLE batches ADD COLUMN chill_seconds INTEGER;
ALTER TABLE batches ADD COLUMN heat_seconds INTEGER;
ALTER TABLE batches ADD COLUMN mol_headspace REAL;    -- CO2 in the headspace
ALTER TABLE batches ADD COLUMN mol_dissolved REAL;    -- CO2 in solution
ALTER TABLE batches ADD COLUMN mol_ejected REAL;      -- CO2 vented, cumulative
ALTER TABLE batches ADD COLUMN expansions INTEGER;    -- expansion-tank reliefs
ALTER TABLE batches ADD COLUMN dumped_volume REAL;    -- L removed by Dump tasks
