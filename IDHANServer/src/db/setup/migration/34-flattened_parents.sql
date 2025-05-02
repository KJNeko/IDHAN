CREATE MATERIALIZED VIEW flattened_parents AS
(
WITH tag_hierarchy AS (
    -- Start with all relationships that have a child
    WITH RECURSIVE chain AS (
        -- Base case: Get all relationships
        SELECT domain_id,
               parent_id,
               child_id,
               ARRAY [parent_id, child_id] as path,
               false                       as is_cyclic -- Initialize as non-cyclic
        FROM aliased_parents

        UNION ALL

        -- Recursive case: Join with aliased_parents to find next level
        SELECT c.domain_id                as domain_id,
               c.parent_id                as parent_id,
               ap.child_id                as child_id,
               c.path || ap.child_id      as path,
               ap.child_id = ANY (c.path) as is_cyclic -- Check if the child is already in the path
        FROM chain c
                 JOIN aliased_parents ap
                      ON c.child_id = ap.parent_id
                          AND ap.domain_id = c.domain_id
        WHERE NOT c.is_cyclic -- Stop recursion if we already found a cycle
    )
    -- Get the final collapsed relationships
    SELECT DISTINCT domain_id,
                    parent_id          as parent_id,
                    child_id           as child_id,
                    bool_or(is_cyclic) as is_cyclic -- Aggregate cyclic status
    FROM chain
    GROUP BY domain_id, child_id, parent_id)
SELECT domain_id,
       parent_id,
       child_id
FROM tag_hierarchy);