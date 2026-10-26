CREATE VIEW misisng_files AS
(
SELECT record_id
FROM file_info
WHERE cluster_delete_time IS NULL
  AND cluster_id IS NULL);