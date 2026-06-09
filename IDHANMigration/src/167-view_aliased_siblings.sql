CREATE OR REPLACE VIEW aliased_siblings AS
WITH all_sibling_pairs AS (SELECT older_id, younger_id, tag_domain_id
                           FROM tag_siblings
                           UNION
                           SELECT effective_older_id, effective_younger_id, tag_domain_id
                           FROM tag_siblings
                           WHERE effective_older_id != older_id
                              OR effective_younger_id != younger_id)
SELECT DISTINCT COALESCE(oa.effective_tag_id, asp.older_id)   AS older_id,
                COALESCE(ya.effective_tag_id, asp.younger_id) AS younger_id,
                asp.tag_domain_id
FROM all_sibling_pairs asp
         LEFT JOIN tag_aliases oa ON oa.aliased_id = asp.older_id AND oa.tag_domain_id = asp.tag_domain_id
         LEFT JOIN tag_aliases ya ON ya.aliased_id = asp.younger_id AND ya.tag_domain_id = asp.tag_domain_id
WHERE asp.older_id != asp.younger_id;
