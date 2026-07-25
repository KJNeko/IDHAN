-- Temporary session keys.
--
-- A client exchanges its permanent API key once (POST /auth/session) for a session key with a
-- fixed lifetime, then presents that session key exactly like an API key. This keeps the permanent
-- key out of the browser after login, and session keys can expire and be revoked without touching
-- the permanent key.
--
-- session_key is stored verbatim, like auth_keys.key_hash: the keys carry 256 bits of entropy, so
-- guessing is not the threat model and hashing would buy nothing against it.
CREATE TABLE auth_sessions
(
    session_key BYTEA PRIMARY KEY CHECK ( length(session_key) = 32 ),
    key_id      INT         NOT NULL REFERENCES auth_keys ( key_id ) ON DELETE CASCADE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at  TIMESTAMPTZ NOT NULL
);

-- Supports both the expiry filter on every authenticated request and bulk cleanup of stale rows.
CREATE INDEX auth_sessions_expires_at_idx ON auth_sessions ( expires_at );
