CREATE MATERIALIZED VIEW active_records AS
(
SELECT DISTINCT record_id
FROM file_info
WHERE cluster_delete_time IS NULL );