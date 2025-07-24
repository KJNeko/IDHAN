CREATE MATERIALIZED VIEW active_total_mapping_counts AS
(
SELECT DISTINCT tag_id, record_id
FROM active_tag_mappings
UNION
DISTINCT
SELECT DISTINCT tag_id, record_id
FROM active_tag_mappings_virtual );

CREATE MATERIALIZED VIEW total_mapping_counts AS
(
WITH distinct_mappings AS (SELECT DISTINCT tag_id, record_id
                           FROM tag_mappings
                           UNION
                           DISTINCT
                           SELECT DISTINCT tag_id, record_id
                           FROM tag_mappings_virtual)
SELECT tag_id,
       COUNT(*
       ) AS count
FROM distinct_mappings
GROUP BY tag_id
    );