CREATE MATERIALIZED VIEW active_filtered_mappings AS
(
WITH mapping_groups AS (
    -- Group mappings by record and domain
    SELECT record_id, domain_id, array_agg(tag_id) as tags
    FROM active_parented_mappings
    GROUP BY record_id, domain_id)
SELECT mg.record_id,
       mg.domain_id,
       t.tag_id
FROM mapping_groups mg
         CROSS JOIN UNNEST(mg.tags) as t(tag_id)
WHERE NOT EXISTS (
    -- Check if this tag is a younger sibling of any other tag in the same group
    SELECT 1
    FROM parented_siblings ps
    WHERE ps.younger_id = t.tag_id
      AND ps.older_id = ANY (mg.tags)
      AND ps.domain_id = mg.domain_id));