-- $1: ClusterID; $2: exclusive RecordID cursor; $3: page size.
SELECT fi.record_id,
       r.sha256,
       COALESCE(fi.extension, m.best_extension, '')
FROM file_info fi
         JOIN records r USING (record_id)
         LEFT JOIN mime m USING (mime_id)
WHERE fi.cluster_id = $1
  AND fi.record_id > $2
ORDER BY fi.record_id
LIMIT $3;
