ALTER TABLE download_session_records
    ADD COLUMN was_imported BOOLEAN NOT NULL DEFAULT false;

-- Older sessions retain only their current URL tree. Backfill imports still evidenced there.
UPDATE download_session_records r
SET was_imported = true
WHERE EXISTS (SELECT 1
              FROM download_session_urls u
              WHERE u.download_session_id = r.download_session_id
                AND u.record_id = r.record_id
                AND u.state = 'imported');
