CREATE INDEX ON file_info (size, record_id);
CREATE INDEX ON file_info (cluster_store_time, record_id);
CREATE INDEX ON file_info (mime_id, record_id);
CREATE INDEX ON file_info (file_mtime, record_id) WHERE file_mtime IS NOT NULL;
