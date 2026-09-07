-- Requeue both records after detaching the inferior to restore distances excluded by its former
-- group membership.
CREATE OR REPLACE FUNCTION remove_duplicate_pair(
    record_a INTEGER,
    record_b INTEGER
) RETURNS BOOLEAN AS
$$
DECLARE
    removed_duplicate_id INTEGER;
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);

    DELETE
    FROM duplicate_group_inferiors inferior
        USING duplicate_groups duplicate_group
    WHERE duplicate_group.duplicate_id = inferior.duplicate_id
      AND (
        (duplicate_group.king_id = record_a AND inferior.inferior_id = record_b)
            OR
        (duplicate_group.king_id = record_b AND inferior.inferior_id = record_a)
        )
    RETURNING inferior.duplicate_id INTO removed_duplicate_id;

    IF removed_duplicate_id IS NULL THEN
        RETURN FALSE;
    END IF;

    DELETE
    FROM duplicate_groups duplicate_group
    WHERE duplicate_group.duplicate_id = removed_duplicate_id
      AND NOT EXISTS (SELECT 1
                      FROM duplicate_group_inferiors inferior
                      WHERE inferior.duplicate_id = duplicate_group.duplicate_id);

    INSERT INTO hamming_distance_queue (record_id)
    VALUES (record_a),
           (record_b)
    ON CONFLICT (record_id) DO NOTHING;

    RETURN TRUE;
END;
$$ LANGUAGE plpgsql;
