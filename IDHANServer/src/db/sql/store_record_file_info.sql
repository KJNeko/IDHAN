-- $1: RecordID; $2: ClusterID; $3: size; $4: optional MimeID; $5: extension.
-- $6: modification time in epoch microseconds; $7: optional birth time in epoch microseconds.
INSERT INTO file_info (record_id, cluster_id, size, mime_id, extension,
                       file_mtime, file_ctime, cluster_store_time)
VALUES ($1, $2, $3, $4, $5,
        TIMESTAMP 'epoch' + $6::bigint * INTERVAL '1 microsecond',
        TIMESTAMP 'epoch' + $7::bigint * INTERVAL '1 microsecond',
        now())
ON CONFLICT (record_id) DO UPDATE SET cluster_id         = $2,
                                      size               = $3,
                                      mime_id            = $4,
                                      extension          = $5,
                                      file_mtime         = EXCLUDED.file_mtime,
                                      file_ctime         = EXCLUDED.file_ctime,
                                      cluster_store_time = COALESCE(file_info.cluster_store_time, now());
