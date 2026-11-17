ALTER TABLE subscriptions
    ADD COLUMN name                  TEXT        NOT NULL DEFAULT '',
    ADD COLUMN enabled               BOOLEAN     NOT NULL DEFAULT true,
    ADD COLUMN interval_seconds      BIGINT      NOT NULL DEFAULT 86400 CHECK (interval_seconds BETWEEN 60 AND 31536000),
    ADD COLUMN known_limit           INTEGER     NOT NULL DEFAULT 20 CHECK (known_limit BETWEEN 0 AND 10000),
    ADD COLUMN next_run_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    ADD COLUMN initial_sync_complete BOOLEAN     NOT NULL DEFAULT false,
    ADD COLUMN active_session_id     BIGINT      REFERENCES download_sessions (download_session_id) ON DELETE SET NULL,
    ADD COLUMN last_session_id       BIGINT      REFERENCES download_sessions (download_session_id) ON DELETE SET NULL,
    ADD COLUMN last_finished_at      TIMESTAMPTZ,
    ADD COLUMN last_error            TEXT;

CREATE INDEX subscriptions_due_idx ON subscriptions (next_run_at)
    WHERE enabled AND active_session_id IS NULL;
