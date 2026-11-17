ALTER TABLE download_sessions
    ADD COLUMN options jsonb NOT NULL DEFAULT '{}'::jsonb;
