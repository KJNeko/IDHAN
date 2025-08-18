CREATE VIEW active_tag_mappings_final AS
(
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, tag_domain_id
FROM active_tag_mappings
UNION ALL
SELECT record_id, tag_id, tag_domain_id
FROM active_tag_mappings_parents);