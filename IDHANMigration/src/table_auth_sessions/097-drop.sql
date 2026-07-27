-- Session keys were removed: the permanent API key is now presented directly on every request, so
-- the table that backed the exchanged session keys is no longer used. Drop it. (The create migration
-- is retained for historical ordering; on a fresh database it creates the table and this drops it.)
DROP TABLE IF EXISTS auth_sessions;
