ALTER TABLE file_info
    RENAME CONSTRAINT file_info_reorder_pkey TO file_info_pkey;
ALTER TABLE file_info
    RENAME CONSTRAINT file_info_reorder_record_id_fkey TO file_info_record_id_fkey;
ALTER TABLE file_info
    RENAME CONSTRAINT file_info_reorder_cluster_id_fkey TO file_info_cluster_id_fkey;
