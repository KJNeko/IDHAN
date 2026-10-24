-- $1: RecordID. Returns the assigned cluster, hash, and resolved extension.
SELECT fi.cluster_id,
       r.sha256,
       COALESCE(fi.extension, m.best_extension, '')
FROM records r
         JOIN file_info fi USING (record_id)
         LEFT JOIN mime m USING (mime_id)
WHERE r.record_id = $1
  AND fi.cluster_id IS NOT NULL;
