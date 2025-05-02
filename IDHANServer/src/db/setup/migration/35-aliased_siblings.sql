CREATE MATERIALIZED VIEW aliased_siblings AS
(
SELECT DISTINCT ts.domain_id, coalesce(tao.alias_id, ts.older_id) as older_id, coalesce(tay.alias_id, ts.younger_id) as younger_id
FROM tag_siblings ts
         LEFT JOIN tag_aliases tao ON tao.domain_id = ts.domain_id AND tao.aliased_id = ts.older_id
         LEFT JOIN tag_aliases tay ON tay.domain_id = ts.domain_id AND tay.aliased_id = ts.younger_id
    )