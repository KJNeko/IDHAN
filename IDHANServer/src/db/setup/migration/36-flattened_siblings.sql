CREATE MATERIALIZED VIEW flattened_siblings AS
WITH RECURSIVE sibling_chain AS (
    -- Base case: direct sibling relationships
    SELECT a.domain_id                      as domain_id,
           a.older_id                       as older_id,
           a.younger_id                     as younger_id,
           ARRAY [a.older_id, a.younger_id] as path,
           1                                as depth
    FROM aliased_siblings a

    UNION

    -- Recursive case: find transitive relationships
    SELECT sc.domain_id            as domain_id,
           sc.older_id             as older_id,
           a.younger_id            as younger_id,
           sc.path || a.younger_id as path,
           sc.depth + 1            as depth
    FROM sibling_chain sc
             JOIN aliased_siblings a ON sc.domain_id = a.domain_id AND sc.younger_id = a.older_id
    WHERE NOT a.younger_id = ANY (sc.path) -- Prevent cycles
)
SELECT DISTINCT domain_id,
                sc.older_id   as older_id,
                sc.younger_id as younger_id
FROM sibling_chain sc
WHERE sc.older_id <> sc.younger_id;