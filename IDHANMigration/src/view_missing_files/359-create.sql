CREATE OR REPLACE VIEW missing_files AS
SELECT file_info.record_id
FROM file_info
WHERE file_info.cluster_id IS NULL
  AND file_info.cluster_delete_time IS NULL;
