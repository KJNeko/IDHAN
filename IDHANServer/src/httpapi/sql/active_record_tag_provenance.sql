-- $1: RecordID. Explicit/aliased tags carry their original tag; inherited tags carry their origin.
SELECT COALESCE(ideal_tag_id, tag_id) AS effective_tag_id,
       tag_domain_id,
       tag_id                         AS original_tag_id,
       NULL::INTEGER                  AS origin_id
FROM active_tag_mappings
WHERE record_id = $1
UNION ALL
SELECT tag_id,
       tag_domain_id,
       NULL::INTEGER AS original_tag_id,
       origin_id
FROM active_tag_mappings_parents
WHERE record_id = $1;
