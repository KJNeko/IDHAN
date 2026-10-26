ALTER TABLE download_session_urls
    ADD COLUMN finished_at TIMESTAMP WITH TIME ZONE,
    ADD COLUMN error       TEXT,
    ADD CONSTRAINT download_session_urls_state_check
        CHECK (state IN ('pending', 'processing', 'completed', 'failed')),
    ADD CONSTRAINT download_session_urls_terminal_fields_check
        CHECK (
            (state IN ('pending', 'processing') AND finished_at IS NULL AND error IS NULL)
                OR (state = 'completed' AND finished_at IS NOT NULL AND error IS NULL)
                OR (state = 'failed' AND finished_at IS NOT NULL AND error IS NOT NULL AND error <> '')
            );

CREATE INDEX download_session_urls_nonterminal_idx
    ON download_session_urls (download_session_url_id)
    WHERE state IN ('pending', 'processing');
