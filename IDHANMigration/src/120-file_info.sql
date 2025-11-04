ALTER TABLE file_info
    ALTER COLUMN cluster_store_time SET DEFAULT now();

ALTER TABLE file_info
    ADD COLUMN modified_time TIMESTAMP;

UPDATE file_info
SET cluster_store_time = now()
WHERE cluster_store_time IS NULL;

ALTER TABLE file_info
    ALTER COLUMN cluster_store_time SET NOT NULL;