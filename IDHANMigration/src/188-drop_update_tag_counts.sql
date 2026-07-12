-- References total_tag_counts, dropped in migration 110. Unreferenced dead code; would throw if
-- ever called.
DROP FUNCTION IF EXISTS update_tag_counts(INTEGER);
