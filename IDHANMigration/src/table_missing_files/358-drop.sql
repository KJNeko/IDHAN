UPDATE file_info fi
SET cluster_id = NULL
FROM missing_files mf
WHERE fi.record_id = mf.record_id
  AND fi.cluster_delete_time IS NULL;

DROP TABLE missing_files;
