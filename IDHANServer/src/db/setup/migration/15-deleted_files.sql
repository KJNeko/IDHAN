CREATE TABLE deleted_files
(
	record_id    INTEGER REFERENCES records (record_id),
	deleted_time TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP
);