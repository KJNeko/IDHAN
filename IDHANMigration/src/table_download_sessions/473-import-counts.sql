ALTER TABLE download_sessions
    ADD COLUMN total_imported_count     BIGINT NOT NULL DEFAULT 0 CHECK (total_imported_count >= 0),
    ADD COLUMN last_scan_imported_count BIGINT CHECK (last_scan_imported_count >= 0);

UPDATE download_sessions s
SET total_imported_count     = (SELECT count(*)
                                FROM download_session_records r
                                WHERE r.download_session_id = s.download_session_id
                                  AND r.was_imported),
    last_scan_imported_count = (SELECT imported_count
                                FROM download_session_results r
                                WHERE r.download_session_id = s.download_session_id);
