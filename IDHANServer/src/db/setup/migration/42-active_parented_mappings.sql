CREATE MATERIALIZED VIEW active_parented_mappings AS
(
SELECT aam.record_id, ap.parent_id as tag_id, aam.domain_id
FROM active_aliased_mappings aam
         JOIN aliased_parents ap ON aam.domain_id = ap.domain_id AND aam.tag_id = ap.child_id
UNION
DISTINCT
SELECT record_id, tag_id, domain_id
FROM active_aliased_mappings);