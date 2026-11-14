CREATE TABLE download_session_results
(
    download_session_id BIGINT PRIMARY KEY REFERENCES download_sessions (download_session_id) ON DELETE CASCADE,
    imported_count      BIGINT      NOT NULL CHECK (imported_count >= 0),
    known_count         BIGINT      NOT NULL CHECK (known_count >= 0),
    error               TEXT,
    finished_at         TIMESTAMPTZ NOT NULL DEFAULT now()
);
