-- Splits imports out of 'completed' and gives each kind of skip its own state, so the outcome is
-- carried by the state instead of a stored sentence. Failure details move to
-- download_session_url_failures.
ALTER TABLE download_session_urls
    DROP CONSTRAINT download_session_urls_state_check,
    DROP CONSTRAINT download_session_urls_terminal_fields_check;

UPDATE download_session_urls
SET state = 'imported_previously'
WHERE state = 'completed'
  AND record_id IS NOT NULL
  AND note IN ('Already in the database', 'Already imported earlier in this session');

UPDATE download_session_urls
SET state = 'imported'
WHERE state = 'completed'
  AND record_id IS NOT NULL;

UPDATE download_session_urls
SET state = 'skipped_already_seen'
WHERE state = 'skipped'
  AND note = 'Already seen in this session';

UPDATE download_session_urls
SET state = 'skipped_already_imported'
WHERE state = 'skipped'
  AND note = 'Already in the database';

-- Filtering by URL class was the only other way a URL was skipped.
UPDATE download_session_urls
SET state = 'skipped_no_url_class'
WHERE state = 'skipped';

ALTER TABLE download_session_urls
    DROP COLUMN note;

ALTER TABLE download_session_urls
    ADD CONSTRAINT download_session_urls_state_check
        CHECK (state IN ('pending', 'processing', 'completed', 'imported', 'imported_previously',
                         'skipped_no_url_class', 'skipped_already_seen', 'skipped_already_imported', 'failed',
                         'cancelled')),
    ADD CONSTRAINT download_session_urls_finished_check
        CHECK ((state IN ('pending', 'processing')) = (finished_at IS NULL));
