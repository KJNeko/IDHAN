
CREATE INDEX ON file_info (size, record_id);
CREATE INDEX ON file_info (cluster_store_time, record_id);
CREATE INDEX ON file_info (mime_id, record_id);
CREATE INDEX ON file_info (modified_time, record_id) WHERE modified_time IS NOT NULL;
