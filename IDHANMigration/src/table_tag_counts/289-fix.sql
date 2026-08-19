UPDATE tag_counts tc
SET storage_count = 0,
    display_count = 0
WHERE NOT EXISTS (SELECT 1
                  FROM active_tag_mappings m
                  WHERE m.tag_id = tc.tag_id
                    AND m.tag_domain_id = tc.tag_domain_id)
  AND NOT EXISTS (SELECT 1
                  FROM active_tag_mappings_final f
                  WHERE f.tag_id = tc.tag_id
                    AND f.tag_domain_id = tc.tag_domain_id)
  AND (tc.storage_count <> 0 OR tc.display_count <> 0);

WITH storage AS (SELECT tag_id, tag_domain_id, COUNT(*)::int AS total
                 FROM active_tag_mappings
                 GROUP BY tag_id, tag_domain_id),
     display AS (SELECT tag_id, tag_domain_id, COUNT(*)::int AS total
                 FROM active_tag_mappings_final
                 GROUP BY tag_id, tag_domain_id)
INSERT
INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count)
SELECT tag_id,
       tag_domain_id,
       COALESCE(s.total, 0) AS storage_count,
       COALESCE(d.total, 0) AS display_count
FROM storage s
         FULL JOIN display d USING (tag_id, tag_domain_id)
ON CONFLICT (tag_id, tag_domain_id)
    DO UPDATE SET storage_count = excluded.storage_count,
                  display_count = excluded.display_count;
