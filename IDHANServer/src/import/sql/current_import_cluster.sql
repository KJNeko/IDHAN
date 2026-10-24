-- $1: RecordID. An absent cluster or read-only flag remains NULL.
SELECT file_info.cluster_id,
       file_info.cluster_delete_time IS NULL,
       file_clusters.read_only
FROM file_info
         LEFT JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id
WHERE file_info.record_id = $1
LIMIT 1;
