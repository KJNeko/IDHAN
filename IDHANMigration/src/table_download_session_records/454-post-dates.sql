ALTER TABLE download_session_records
    ADD COLUMN posted_at           TIMESTAMPTZ,
    ADD COLUMN post_dates_complete BOOLEAN NOT NULL DEFAULT false;
