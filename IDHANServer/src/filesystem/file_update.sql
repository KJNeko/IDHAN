INSERT INTO file_info (record_id, size, mime_id, cluster_id, cluster_store_time, extension, cluster_delete_time)
VALUES ($1, $2, $3, $4, $5, $6, NULL)
ON CONFLICT DO UPDATE SET size = $2 AND mime_id = $3 AND cluster_id = $4 AND cluster_store_time = now() AND extension = $5