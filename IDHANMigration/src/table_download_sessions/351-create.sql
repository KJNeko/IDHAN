CREATE TABLE download_sessions
(
    download_session_id BIGSERIAL PRIMARY KEY,
    name                TEXT        NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_used_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX download_sessions_name_lower_idx ON download_sessions (lower(name));

CREATE TABLE download_session_urls
(
    download_session_url_id BIGSERIAL PRIMARY KEY,
    download_session_id     BIGINT      NOT NULL REFERENCES download_sessions (download_session_id) ON DELETE CASCADE,
    url                     TEXT        NOT NULL,
    state                   TEXT        NOT NULL DEFAULT 'pending',
    created_at              TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX download_session_urls_session_created_idx
    ON download_session_urls (download_session_id, download_session_url_id DESC);

CREATE TABLE download_session_records
(
    download_session_id BIGINT      NOT NULL REFERENCES download_sessions (download_session_id) ON DELETE CASCADE,
    record_id           INTEGER     NOT NULL REFERENCES records (record_id) ON DELETE CASCADE,
    imported_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (download_session_id, record_id)
);
