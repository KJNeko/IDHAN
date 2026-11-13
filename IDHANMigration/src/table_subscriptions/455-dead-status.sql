ALTER TABLE subscriptions
    ADD COLUMN latest_post_at      TIMESTAMPTZ,
    ADD COLUMN post_dates_complete BOOLEAN NOT NULL DEFAULT false,
    ADD COLUMN dead                BOOLEAN NOT NULL DEFAULT false;
