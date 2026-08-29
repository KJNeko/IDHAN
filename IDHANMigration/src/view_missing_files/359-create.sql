-- A record whose file no cluster holds, and that was never deleted: the file is gone from where it
-- was expected. Deleted records carry a cluster_delete_time and are not missing.
CREATE OR REPLACE VIEW missing_files AS
SELECT file_info.record_id
FROM file_info
WHERE file_info.cluster_id IS NULL
  AND file_info.cluster_delete_time IS NULL;
