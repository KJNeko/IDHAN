-- Mime ids are now a closed set. Preserve file_info while replacing the old, serially allocated
-- rows, then map each legacy MIME name to its pinned id. A legacy type not represented by the
-- closed set becomes unknown rather than losing the file's size, cluster, timestamps, or extension.
CREATE TEMPORARY TABLE file_info_mime_names ON COMMIT DROP AS
SELECT fi.record_id,
       m.name AS mime_name
FROM file_info fi
         LEFT JOIN mime m USING (mime_id);

ALTER TABLE file_info
    DROP CONSTRAINT IF EXISTS file_info_mime_id_fkey;

TRUNCATE mime;

INSERT INTO mime (mime_id, name, best_extension)
VALUES (1, 'unknown/unknown', ''),
       (1000, 'image/jpeg', 'jpg'),
       (1001, 'image/png', 'png'),
       (1002, 'image/webp', 'webp'),
       (1003, 'image/avif', 'avif'),
       (1004, 'image/tiff', 'tiff'),
       (2000, 'video/mp4', 'mp4'),
       (2001, 'video/mpeg', 'mpeg'),
       (2002, 'video/webm', 'webm'),
       (2003, 'video/quicktime', 'mov'),
       (3000, 'image/gif', 'gif'),
       (3001, 'image/apng', 'png'),
       (5000, 'application/zip', 'zip'),
       (5001, 'application/zip', 'cbz'),
       (5002, 'application/zip', 'zip'),
       (6000, 'application/psd', 'psd'),
       (6001, 'application/x-clip-studio', 'clip');

UPDATE file_info fi
SET mime_id = CASE mapping.mime_name
                  WHEN 'image/jpeg' THEN 1000
                  WHEN 'image/png' THEN 1001
                  WHEN 'image/webp' THEN 1002
                  WHEN 'image/avif' THEN 1003
                  WHEN 'image/tiff' THEN 1004
                  WHEN 'video/mp4' THEN 2000
                  WHEN 'video/mpeg' THEN 2001
                  WHEN 'video/webm' THEN 2002
                  WHEN 'video/quicktime' THEN 2003
                  WHEN 'image/gif' THEN 3000
                  WHEN 'image/apng' THEN 3001
                  WHEN 'application/zip' THEN 5000
                  WHEN 'application/psd' THEN 6000
                  WHEN 'application/x-clip-studio' THEN 6001
                  ELSE 1
    END
FROM file_info_mime_names mapping
WHERE fi.record_id = mapping.record_id
  AND mapping.mime_name IS NOT NULL;

ALTER TABLE file_info
    ADD CONSTRAINT file_info_mime_id_fkey FOREIGN KEY (mime_id) REFERENCES mime (mime_id) ON UPDATE CASCADE;

-- Everything below this is reserved for the blocks the header lays out.
SELECT setval(pg_get_serial_sequence('mime', 'mime_id'), 10000);
