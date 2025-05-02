CREATE TABLE file_metadata
(
    cluster_id SMALLINT REFERENCES file_clusters (cluster_id) NOT NULL,
    record_id  INTEGER REFERENCES records (record_id)         NOT NULL,
    file_size  BIGINT                                         NOT NULL,
    obtained   TIMESTAMP WITHOUT TIME ZONE                    NOT NULL
);