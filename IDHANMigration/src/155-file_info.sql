DELETE
FROM file_info
WHERE cluster_id IS NULL
  AND cluster_delete_time IS NULL;

ALTER TABLE file_info
    ADD CONSTRAINT cluster_id_xor_delete_time
        CHECK (
            (cluster_id IS NOT NULL AND cluster_delete_time IS NULL)
                OR
            (cluster_id IS NULL AND cluster_delete_time IS NOT NULL)
            );
