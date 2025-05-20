CREATE MATERIALIZED VIEW aliased_parents AS
(
SELECT DISTINCT tp.domain_id as domain_id, coalesce(fap.alias_id, tp.parent_id) as parent_id, coalesce(fac.alias_id, tp.child_id) as child_id
FROM tag_parents tp
         LEFT JOIN flattened_aliases fap ON fap.domain_id = tp.domain_id AND fap.aliased_id = tp.parent_id
         LEFT JOIN flattened_aliases fac ON fac.domain_id = tp.domain_id AND fac.aliased_id = tp.child_id
    )