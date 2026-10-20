ALTER TABLE file_clusters
    ADD COLUMN cluster_uuid UUID NOT NULL DEFAULT gen_random_uuid(),
    ADD CONSTRAINT file_clusters_cluster_uuid_key UNIQUE (cluster_uuid);
