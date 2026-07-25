CREATE TABLE file_info
(
    size                BIGINT                                 NOT NULL,
    record_id           INTEGER REFERENCES records (record_id) NOT NULL,
    mime_id             INTEGER REFERENCES mime (mime_id),
    cluster_store_time  TIMESTAMP WITHOUT TIME ZONE            NOT NULL DEFAULT now(), -- Will be set if the file has been stored in a cluster.
    cluster_delete_time TIMESTAMP WITHOUT TIME ZONE,
    modified_time       TIMESTAMP WITHOUT TIME ZONE,
    extension           TEXT,
    cluster_id          SMALLINT REFERENCES file_clusters (cluster_id),                -- Will be null if we have not obtained the file before.
    PRIMARY KEY (record_id),
    CHECK ( NOT (mime_id IS NULL AND extension IS NULL) ),                             -- Enforces that an extension is set if mime_id is not set,
    CHECK ( (cluster_id IS NOT NULL) <> (cluster_delete_time IS NOT NULL) ),           -- Enforces that cluster_delete_time OR cluster_id is set.
    CHECK ( size >= 0 )                                                                -- Enforces that size is non-negative.
);
