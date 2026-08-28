ALTER TABLE archive_map
    ADD COLUMN path TEXT NULL;

UPDATE archive_map map
SET path = md.json ->> encode(r.sha256, 'hex')
FROM records r,
     archive_metadata meta,
     metadata md
WHERE map.record_id = r.record_id
  AND meta.archive_id = map.archive_id
  AND md.record_id = meta.record_id
  AND md.json IS NOT NULL
  AND json_typeof(md.json) = 'object';

DELETE
FROM archive_map
WHERE path IS NULL;

ALTER TABLE archive_map
    ALTER COLUMN path SET NOT NULL,
    DROP CONSTRAINT IF EXISTS archive_map_archive_id_record_id_key,
    ADD CONSTRAINT archive_map_archive_id_path_key UNIQUE (archive_id, path);

CREATE INDEX archive_map_record_id_idx ON archive_map (record_id);

UPDATE metadata md
SET json = (SELECT COALESCE(json_object_agg(key, value), '{}'::json)
            FROM json_each(md.json)
            WHERE key !~ '^[0-9a-f]{64}$')
WHERE md.json IS NOT NULL
  AND json_typeof(md.json) = 'object'
  AND EXISTS (SELECT 1 FROM archive_metadata meta WHERE meta.record_id = md.record_id);
