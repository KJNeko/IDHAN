-- Adds the record's own creation time, distinct from file_info.cluster_store_time (file import time) —
-- a record can exist without an associated file in IDHAN's model. Pre-existing rows are backfilled with
-- this migration's execution time, not their true historical creation time, since that was never
-- recorded; RECORD_TIME sort is only meaningful for records created after this migration runs.
ALTER TABLE records
    ADD COLUMN creation_time TIMESTAMP NOT NULL DEFAULT now();
