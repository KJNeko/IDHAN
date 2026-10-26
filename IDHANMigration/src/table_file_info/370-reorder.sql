CREATE TABLE file_info_reorder
(
    size                BIGINT                                         NOT NULL,
    cluster_store_time  TIMESTAMP,
    cluster_delete_time TIMESTAMP,
    file_mtime          TIMESTAMP,
    file_ctime          TIMESTAMP,
    record_id           INTEGER REFERENCES records (record_id)         NOT NULL PRIMARY KEY,
    mime_id             SMALLINT REFERENCES mime (mime_id)             NULL,
    cluster_id          SMALLINT REFERENCES file_clusters (cluster_id) NULL,
    extension           TEXT                                           NULL,
    CHECK (SIZE >= 0),
    CHECK (mime_id IS NOT NULL OR extension IS NOT NULL) -- mime_id or extension must not be null,
);

INSERT INTO file_info_reorder (size, cluster_store_time, cluster_delete_time, file_mtime, record_id, mime_id,
                               cluster_id, extension)
SELECT size,
       cluster_store_time,
       cluster_delete_time,
       modified_time as file_mtime,
       record_id,
       mime_id,
       cluster_id,
       extension
FROM file_info;

-- Drop dependent view

DROP TABLE file_info;
ALTER TABLE file_info_reorder
    RENAME TO file_info;