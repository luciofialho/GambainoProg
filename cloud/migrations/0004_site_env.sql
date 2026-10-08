-- Environments (Povoto/docs/cloud-plan.md, "Ambientes e versões"): one
-- database for production and development; each site says which it is.
-- The development Worker (CLOUD_ENV = "dev") reads every site but writes
-- (ingest, set point requests, batch deletion) only to "dev" sites.
ALTER TABLE sidekicks ADD COLUMN env TEXT NOT NULL DEFAULT 'prod' CHECK (env IN ('prod', 'dev'));
