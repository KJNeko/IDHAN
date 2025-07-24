CREATE OR REPLACE VIEW tag_mappings_final AS
(
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, domain_id
FROM tag_mappings
UNION ALL
SELECT record_id, tag_id, domain_id
FROM tag_mappings_virtual
    )