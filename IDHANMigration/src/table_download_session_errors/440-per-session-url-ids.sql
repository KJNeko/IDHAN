ALTER TABLE download_session_errors
    RENAME COLUMN download_session_url_id TO legacy_url_id;

ALTER TABLE download_session_errors
    ADD COLUMN download_session_url_id INTEGER;

UPDATE download_session_errors AS logged
SET download_session_url_id = urls.download_session_url_id
FROM download_session_urls AS urls
WHERE urls.download_session_id = logged.download_session_id
  AND urls.legacy_url_id = logged.legacy_url_id;

-- Also drops download_session_errors_retry_pending_idx, which the composite index below replaces.
ALTER TABLE download_session_errors
    DROP COLUMN legacy_url_id;

ALTER TABLE download_session_errors
    ADD CONSTRAINT download_session_errors_url_fkey
        FOREIGN KEY (download_session_id, download_session_url_id)
            REFERENCES download_session_urls (download_session_id, download_session_url_id)
            ON DELETE SET NULL (download_session_url_id);

CREATE INDEX download_session_errors_url_idx
    ON download_session_errors (download_session_id, download_session_url_id)
    WHERE download_session_url_id IS NOT NULL;
