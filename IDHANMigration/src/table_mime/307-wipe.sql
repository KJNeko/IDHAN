-- The mime table is reseeded from the closed set in IDHAN/include/MimeIDs.hpp by
-- mime::registerMimeTypes() once migrations have run, so the old rows go here.

-- file_info is the only table the cascade reaches that carries triggers, and none of them have
-- anything to do here. A migration file runs as one multi-statement exec, which postgres wraps in an
-- implicit transaction, so a failure rolls the triggers back on with the truncate.
ALTER TABLE file_info
    DISABLE TRIGGER USER;

TRUNCATE mime CASCADE;

ALTER TABLE file_info
    ENABLE TRIGGER USER;

-- TRUNCATE fires no delete triggers, so the cluster counters still describe the rows it dropped.
UPDATE file_clusters
SET size_used  = 0,
    file_count = 0
WHERE size_used <> 0
   OR file_count <> 0;

-- Everything below this is reserved for the blocks the header lays out.
SELECT setval(pg_get_serial_sequence('mime', 'mime_id'), 10000);
