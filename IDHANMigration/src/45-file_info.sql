CREATE TABLE file_info
(
    size                BIGINT                                        NOT NULL,
    record_id           INTEGER UNIQUE REFERENCES records (record_id) NOT NULL,
    mime_id             INTEGER REFERENCES mime (mime_id),
    cluster_store_time  TIMESTAMP WITHOUT TIME ZONE,                    -- Will be set if the file has been stored in a cluster.
    cluster_delete_time TIMESTAMP WITHOUT TIME ZONE,
    extension           TEXT,
    cluster_id          SMALLINT REFERENCES file_clusters (cluster_id), -- Will be null if we have not obtained the file before.
    PRIMARY KEY (record_id)
);

CREATE INDEX ON file_info (record_id);