-- Drops the alias resolution from the parents branch of 034-create.sql.
--
-- 034 resolved parent tag ids at read time:
--     LEFT JOIN tag_aliases ta ON ta.aliased_id = atmp.tag_id AND ...
--     COALESCE(ta.effective_tag_id, atmp.tag_id) AS tag_id
-- because active_tag_mappings_parents.tag_id could hold a tag that was aliased after the parent
-- row was materialised. func_repair_parents_on_alias/098-create closes that write path, so the
-- stored id is now always the ideal one and the COALESCE is redundant.
--
-- It was also the reason the parents branch could not use an index. Putting the search predicate
-- on the output of the join means `tag_id = <id>` cannot be pushed down to
-- active_tag_mappings_parents (tag_id) INCLUDE (record_id) (043-index): the planner has to
-- materialise the join first. Measured on a 3M-row parents table with 195k aliases, one lookup
-- through the view read 20338 buffers in 171 ms -- a sequential scan of every parent row plus a
-- sequential scan of every tag_aliases partition, hash-joined, to return 0 rows from that branch.
-- Without the COALESCE the same lookup is an index-only scan: 31 buffers, 0.449 ms.
--
-- Both branches are disjoint by construction, so UNION ALL still needs no dedup.

CREATE OR REPLACE VIEW active_tag_mappings_final AS
(
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, tag_domain_id
FROM active_tag_mappings
UNION ALL
SELECT record_id, tag_id, tag_domain_id
FROM active_tag_mappings_parents);
