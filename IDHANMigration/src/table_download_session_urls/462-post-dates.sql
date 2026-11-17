ALTER TABLE download_session_urls
    ADD COLUMN posted_at           TIMESTAMPTZ,
    ADD COLUMN post_dates_complete BOOLEAN NOT NULL DEFAULT false;

UPDATE download_session_urls urls
SET posted_at=records.posted_at,
    post_dates_complete=records.post_dates_complete
FROM download_session_records records
WHERE urls.download_session_id = records.download_session_id
  AND urls.record_id = records.record_id;
