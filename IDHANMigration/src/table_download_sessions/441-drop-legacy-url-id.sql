-- Also drops the global sequence that column owned.
ALTER TABLE download_session_urls
    DROP COLUMN legacy_url_id;
