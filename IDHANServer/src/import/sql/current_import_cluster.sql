-- $1: RecordID. Configuration comes from ClusterManager; assignments remain live.
SELECT cluster_id, cluster_delete_time IS NULL
FROM file_info
WHERE record_id = $1
LIMIT 1;
