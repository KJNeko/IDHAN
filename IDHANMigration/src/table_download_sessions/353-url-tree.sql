ALTER TABLE download_session_urls
    ADD COLUMN parent_url_id BIGINT REFERENCES download_session_urls (download_session_url_id) ON DELETE CASCADE,
    ADD COLUMN record_id     INTEGER REFERENCES records (record_id) ON DELETE SET NULL,
    ADD COLUMN note          TEXT,
    DROP CONSTRAINT download_session_urls_state_check,
    DROP CONSTRAINT download_session_urls_terminal_fields_check,
    ADD CONSTRAINT download_session_urls_state_check
        CHECK (state IN ('pending', 'processing', 'completed', 'failed', 'skipped')),
    ADD CONSTRAINT download_session_urls_terminal_fields_check
        CHECK (
            (state IN ('pending', 'processing') AND finished_at IS NULL AND error IS NULL)
                OR (state = 'completed' AND finished_at IS NOT NULL AND error IS NULL)
                OR (state = 'failed' AND finished_at IS NOT NULL AND error IS NOT NULL AND error <> '')
                OR (state = 'skipped' AND finished_at IS NOT NULL AND error IS NULL)
            );

CREATE INDEX download_session_urls_parent_idx
    ON download_session_urls (parent_url_id, download_session_url_id)
    WHERE parent_url_id IS NOT NULL;
