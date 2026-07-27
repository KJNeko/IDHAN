-- UNION ALL (not UNION DISTINCT): the two branches are disjoint by construction, so dedup is
-- wasted work. Parent rows are alias-resolved through tag_aliases so the view never surfaces an
-- un-aliased parent id.
CREATE VIEW active_tag_mappings_final AS
(
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, tag_domain_id
FROM active_tag_mappings
UNION ALL
SELECT atmp.record_id,
       COALESCE(ta.effective_tag_id, atmp.tag_id) AS tag_id,
       atmp.tag_domain_id
FROM active_tag_mappings_parents atmp
         LEFT JOIN tag_aliases ta ON ta.aliased_id = atmp.tag_id AND ta.tag_domain_id = atmp.tag_domain_id);
