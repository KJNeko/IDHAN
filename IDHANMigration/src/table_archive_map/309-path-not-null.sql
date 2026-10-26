ALTER TABLE archive_map
    DROP CONSTRAINT IF EXISTS archive_map_archive_id_path_key;

DELETE
FROM archive_map
WHERE path IS NULL;

ALTER TABLE archive_map
    ALTER COLUMN path SET NOT NULL,
    ADD CONSTRAINT archive_map_archive_id_path_key UNIQUE (archive_id, path);
