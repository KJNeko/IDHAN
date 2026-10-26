CREATE OR REPLACE FUNCTION remove_duplicate_pair(record_a INTEGER, record_b INTEGER) RETURNS BOOLEAN AS
$$
DECLARE
    affected_ids INTEGER[];
    removed      INTEGER;
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);
    SELECT record_id
    INTO removed
    FROM duplicate_members
    WHERE record_id <> king_id
      AND ((record_id = record_a AND king_id = record_b)
        OR (record_id = record_b AND king_id = record_a));
    IF removed IS NULL THEN
        RETURN FALSE;
    END IF;

    SELECT array_agg(record_id)
    INTO affected_ids
    FROM duplicate_group_members(duplicate_king(removed)) AS member(record_id);
    -- Detach only the selected member; the existing king and all other members stay in place.
    UPDATE duplicate_members SET king_id = removed WHERE record_id = removed;

    INSERT INTO hamming_distance_queue (record_id)
    SELECT unnest(affected_ids)
    ON CONFLICT (record_id) DO NOTHING;
    RETURN TRUE;
END;
$$ LANGUAGE plpgsql;
