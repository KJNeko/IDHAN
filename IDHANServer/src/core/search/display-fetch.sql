-- Optimized for getting the display tags from a single record.
WITH mappings AS (SELECT record_id, tag_id, domain_id FROM tag_mappings WHERE record_id = $1),
     aliased_mappings AS (SELECT DISTINCT record_id, coalesce(fa.alias_id, tm.tag_id) as tag_id, tm.domain_id
                          FROM mappings tm
                                   LEFT JOIN flattened_aliases fa
                                             ON tm.domain_id = fa.domain_id AND tm.tag_id = fa.aliased_id),
     parented_mappings AS (SELECT DISTINCT tm.record_id, tm.domain_id, fp.parent_id as tag_id
                           FROM mappings tm
                                    JOIN flattened_parents fp
                                         ON fp.domain_id = tm.domain_id AND fp.child_id = tm.tag_id),
     combined_mappings AS (SELECT record_id, tag_id, domain_id
                           FROM aliased_mappings
                           UNION
                           DISTINCT
                           SELECT record_id, tag_id, domain_id
                           FROM parented_mappings)
SELECT *
FROM combined_mappings;
