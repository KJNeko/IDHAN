CREATE MATERIALIZED VIEW flattened_aliases AS
(
WITH tag_relationship_checks AS (
    -- Check if tag has relationships with other tags
    SELECT DISTINCT domain_id,
                    aliased_id,
                    alias_id,
                    EXISTS(SELECT 1
                           FROM tag_aliases ta2
                           WHERE ta1.domain_id = ta2.domain_id
                             AND ta1.aliased_id = ta2.alias_id) as has_incoming,
                    EXISTS(SELECT 1
                           FROM tag_aliases ta2
                           WHERE ta1.domain_id = ta2.domain_id
                             AND ta1.alias_id = ta2.aliased_id) as has_outgoing
    FROM tag_aliases ta1),
     independent_tags AS (
         -- Tags with no relationships in either direction
         SELECT domain_id, aliased_id, alias_id
         FROM tag_relationship_checks
         WHERE has_incoming = false
           AND has_outgoing = false),
     intermediate_tags AS (
         -- Tags with both incoming and outgoing relationships
         SELECT domain_id, aliased_id, alias_id
         FROM tag_relationship_checks
         WHERE has_incoming = true
           AND has_outgoing = true),
     connected_tags AS (
         -- Combine intermediate and root tags
         SELECT domain_id, aliased_id, alias_id
         FROM tag_relationship_checks
         WHERE has_outgoing),
     tag_hierarchy AS (WITH RECURSIVE hierarchy AS (
         -- Base case: start with connected tags
         SELECT domain_id,
                aliased_id,
                false            as is_final,
                alias_id,
                ARRAY [alias_id] as path,
                false            as is_cyclic,
                1                as depth
         FROM connected_tags

         UNION ALL

         -- Recursive case: traverse tag relationships
         SELECT h.domain_id,
                h.aliased_id,
                ca.alias_id IS NULL               as is_final,
                COALESCE(ca.alias_id, h.alias_id) as alias_id,
                array_append(h.path, ca.alias_id),
                ca.alias_id = ANY (h.path)        as is_cyclic,
                h.depth + 1
         FROM hierarchy h
                  LEFT JOIN intermediate_tags ca ON h.alias_id = ca.aliased_id
             AND h.domain_id = ca.domain_id
         WHERE NOT h.is_cyclic
           AND NOT h.is_final)
                       SELECT DISTINCT domain_id, aliased_id, alias_id
                       FROM hierarchy
                       WHERE is_final
                         AND NOT is_cyclic

                       UNION

                       SELECT domain_id, aliased_id, alias_id
                       FROM independent_tags)
SELECT *
FROM tag_hierarchy)