CREATE TABLE IF NOT EXISTS file_clusters
(
    cluster_id         SMALLSERIAL NOT NULL PRIMARY KEY,
    ratio_number       SMALLINT    NOT NULL DEFAULT 1,     -- Ratio of the cluster ( Ratio / TotalRatio )
    size_used          BIGINT      NOT NULL DEFAULT 0,     -- Size used by this cluster
    size_limit         BIGINT      NOT NULL DEFAULT 0,     -- Byte limit of the cluster
    file_count         INTEGER     NOT NULL DEFAULT 0,     -- Number of files in this cluster
    read_only          BOOLEAN     NOT NULL DEFAULT TRUE,  -- If true then we can only read files from this cluster.
    allowed_thumbnails BOOLEAN     NOT NULL DEFAULT FALSE, -- Allows thumbnails to be stored here
    allowed_files      BOOLEAN     NOT NULL DEFAULT TRUE,  -- Allows files to be stored here
    cluster_name       TEXT UNIQUE,                        -- if null then we use the folder path as the name
    folder_path        TEXT        NOT NULL UNIQUE         -- Path of the folders
);