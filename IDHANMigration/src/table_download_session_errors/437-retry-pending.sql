-- Errors from before a retry are removed only once that retry completes.
ALTER TABLE download_session_errors
    ADD COLUMN retry_pending BOOLEAN NOT NULL DEFAULT false;

CREATE INDEX download_session_errors_retry_pending_idx
    ON download_session_errors (download_session_url_id)
    WHERE retry_pending;
