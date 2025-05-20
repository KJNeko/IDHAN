CREATE MATERIALIZED VIEW aliased_siblings AS
(
SELECT coalesce(fao.alias_id, ts.older_id) as older_id, coalesce(fay.alias_id, ts.younger_id) as younger_id, ts.domain_id
FROM tag_siblings ts
         LEFT JOIN flattened_aliases fao ON fao.domain_id = ts.domain_id AND fao.aliased_id = ts.older_id
         LEFT JOIN flattened_aliases fay ON fay.domain_id = ts.domain_id AND fay.aliased_id = ts.younger_id
    )