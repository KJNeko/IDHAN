CREATE MATERIALIZED VIEW active_aliased_mappings AS
(
SELECT DISTINCT record_id, coalesce(fa.alias_id, am.tag_id) as tag_id, am.domain_id
FROM active_mappings am
         LEFT JOIN flattened_aliases fa ON am.domain_id = fa.domain_id AND am.tag_id = fa.aliased_id
    );