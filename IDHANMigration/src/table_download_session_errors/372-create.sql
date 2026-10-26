CREATE TABLE download_session_errors
(
    download_session_error_id BIGSERIAL PRIMARY KEY,
    download_session_id       BIGINT      NOT NULL REFERENCES download_sessions (download_session_id) ON DELETE CASCADE,
    download_session_url_id   BIGINT      REFERENCES download_session_urls (download_session_url_id) ON DELETE SET NULL,
    url                       TEXT        NOT NULL,
    status                    INTEGER     NOT NULL,
    lane                      TEXT        NOT NULL DEFAULT '',
    message                   TEXT,
    occurred_at               TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX download_session_errors_session_idx
    ON download_session_errors (download_session_id, download_session_error_id DESC);

CREATE INDEX download_session_errors_status_idx
    ON download_session_errors (download_session_id, status, download_session_error_id DESC);
