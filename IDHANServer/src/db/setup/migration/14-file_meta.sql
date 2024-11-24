CREATE TABLE file_metadata
(
	cluster_id SMALLINT REFERENCES file_clusters (cluster_id),
	record_id  INTEGER REFERENCES records (record_id),
	file_size  BIGINT,
	obtained   TIMESTAMP WITHOUT TIME ZONE,
	UNIQUE (record_id)
);