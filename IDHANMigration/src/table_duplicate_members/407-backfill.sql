-- Populate each existing tree once. Isolated records need no stored membership.
INSERT INTO duplicate_members (record_id, king_id)
SELECT record_id, root_id
FROM flattened_duplicates;

-- A cycle has no root and would otherwise be silently omitted by the backfill.
DO
$$
    BEGIN
        IF EXISTS (SELECT 1
                   FROM duplicate_pairs pair
                   WHERE NOT EXISTS (SELECT 1
                                     FROM duplicate_members member
                                     WHERE member.record_id = pair.worse_record_id)) THEN
            RAISE EXCEPTION 'Cannot flatten duplicate comparisons containing a cycle';
        END IF;
    END;
$$;
