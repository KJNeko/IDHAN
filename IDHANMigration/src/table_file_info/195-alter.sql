ALTER TABLE file_info
    ALTER COLUMN cluster_store_time DROP DEFAULT,
    ALTER COLUMN cluster_store_time DROP NOT NULL;
