-- Thumbnails are no longer stored in a file cluster, so allowed_thumbnails gates nothing.
-- allowed_files was never read and is TRUE on every row. Cluster selection uses read_only alone.
ALTER TABLE file_clusters
    DROP COLUMN allowed_thumbnails,
    DROP COLUMN allowed_files;
