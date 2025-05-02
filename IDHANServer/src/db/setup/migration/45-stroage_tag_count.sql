CREATE MATERIALIZED VIEW storage_tag_count AS
(
WITH unique_mappings(tag_id, record_id) AS (SELECT DISTINCT active_mappings.tag_id,
                                                            active_mappings.record_id
                                            FROM active_mappings)
SELECT tag_id,
       count(tag_id) AS count
FROM unique_mappings
GROUP BY tag_id
ORDER BY (count(tag_id)) DESC, tag_id);
