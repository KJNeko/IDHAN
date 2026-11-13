-- The URL key becomes per session, so the reference is rebuilt against the composite key.
ALTER TABLE download_session_errors
    DROP CONSTRAINT download_session_errors_download_session_url_id_fkey;
