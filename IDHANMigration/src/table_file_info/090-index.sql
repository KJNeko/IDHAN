-- Supporting indexes for the sort types added in the preceding commits. Without these, every one
-- of them except HASH (which already piggybacks on records.sha256's UNIQUE index) required a full
-- table scan plus an explicit Sort step on every query — the only existing index across these
-- tables was file_info(record_id), itself redundant with file_info's own PRIMARY KEY.
--
-- Each index leads with the sort column, then record_id, matching generateOrderByClause()'s
-- tiebreak (which now follows the primary sort's direction, see the preceding commit) — a single
-- ascending index on (col, record_id) satisfies ASC queries via a forward scan and DESC queries
-- via a backward scan, without needing a second descending index.
--
-- WIDTH/HEIGHT/RATIO/NUM_PIXELS aren't covered here: they sort by a COALESCE across three
-- separate tables (image_metadata/video_metadata/image_project_metadata), each already reachable
-- via its own UNIQUE(record_id) constraint for the LEFT JOINs, so those joins are already
-- indexed — but no single index can produce the final cross-table order directly, so those four
-- sort types still need an explicit Sort regardless. NUM_TAGS is intentionally left alone too
-- (tracked separately).

CREATE INDEX ON file_info (size, record_id);
CREATE INDEX ON file_info (cluster_store_time, record_id);
CREATE INDEX ON file_info (mime_id, record_id);
-- modified_time has no NOT NULL constraint, and sorting by it always excludes NULLs
-- (generateSortFilterClause()), so a partial index keeps this smaller than indexing every row.
CREATE INDEX ON file_info (modified_time, record_id) WHERE modified_time IS NOT NULL;
