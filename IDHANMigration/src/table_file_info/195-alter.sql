-- cluster_store_time records when a file landed in a cluster, so it has no value until that
-- happens. NOT NULL DEFAULT now() instead stamped it when the row was inserted, which made
-- importFile's store_recorded check true for every row that existed and so reported every import as
-- ImportStatus::Exists. ClusterManager::storeFile is the only writer and already sets it.
--
-- Rows written before this cannot be told apart after the fact, so they keep their existing
-- timestamp and continue to report as already-stored.
ALTER TABLE file_info
    ALTER COLUMN cluster_store_time DROP DEFAULT,
    ALTER COLUMN cluster_store_time DROP NOT NULL;
