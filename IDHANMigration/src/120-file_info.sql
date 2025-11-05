ALTER TABLE file_info
    ALTER COLUMN cluster_store_time SET DEFAULT now();
ALTER TABLE file_info
    ALTER COLUMN cluster_store_time SET NOT NULL;

ALTER TABLE file_info
    ADD COLUMN modified_time TIMESTAMP;