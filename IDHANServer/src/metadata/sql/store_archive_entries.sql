-- $1: ArchiveID; $2: RecordID[]; $3: matching entry paths.
-- DBBindings supplies the PostgreSQL type of the record-ID array, so no C++ type-name substitution is needed.
INSERT INTO archive_map (archive_id, record_id, path)
SELECT $1, entry.record_id, entry.path
FROM UNNEST($2, $3::text[]) AS entry(record_id, path)
ON CONFLICT (archive_id, path) DO UPDATE SET record_id = EXCLUDED.record_id;
