-- 371 created the view as `misisng_files`; every caller queries `missing_files`.
DROP VIEW misisng_files;

CREATE VIEW missing_files AS
SELECT record_id
FROM file_info
WHERE cluster_id IS NULL
  AND cluster_delete_time IS NULL;
