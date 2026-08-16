CREATE TABLE auth_sessions
(
    session_key BYTEA PRIMARY KEY CHECK ( length(session_key) = 32 ),
    key_id      INT         NOT NULL REFERENCES auth_keys ( key_id ) ON DELETE CASCADE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at  TIMESTAMPTZ NOT NULL
);

-- Supports both the expiry filter on every authenticated request and bulk cleanup of stale rows.
CREATE INDEX auth_sessions_expires_at_idx ON auth_sessions ( expires_at );
