ALTER TABLE subscriptions
    ADD COLUMN session_id     BIGINT UNIQUE REFERENCES download_sessions (download_session_id) ON DELETE SET NULL,
    ADD COLUMN active_root_id INTEGER;

UPDATE subscriptions
SET session_id=last_session_id,
    active_root_id=(SELECT max(download_session_url_id)
                    FROM download_session_urls
                    WHERE download_session_id = subscriptions.active_session_id
                      AND parent_url_id IS NULL);
