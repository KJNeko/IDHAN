CREATE TABLE file_info
(
    record_id INTEGER                           NOT NULL,
    size      BIGINT                            NOT NULL,
    mime_id   INTEGER REFERENCES mime (mime_id) NOT NULL,
    cluster_id         SMALLINT REFERENCES file_clusters (cluster_id), -- Will be null if we have not obtained the file before.
    cluster_store_time TIMESTAMP WITHOUT TIME ZONE                     -- Will be set if the file has been stored in a cluster.
);