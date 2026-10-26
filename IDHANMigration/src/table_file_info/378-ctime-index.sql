CREATE INDEX ON file_info (file_ctime, record_id) WHERE file_ctime IS NOT NULL;
