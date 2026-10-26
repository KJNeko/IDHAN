CREATE OR REPLACE FUNCTION insert_duplicate_pair(worse INTEGER, better INTEGER) RETURNS VOID AS
$$
DECLARE
    worse_root   INTEGER;
    better_root  INTEGER;
    new_root     INTEGER;
    moved_ids    INTEGER[];
    affected_ids INTEGER[];
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);
    IF worse = better THEN
        RETURN;
    END IF;


    worse_root := duplicate_king(worse);
    better_root := duplicate_king(better);
    new_root := better_root;
    IF worse_root = better_root THEN
        IF worse <> worse_root THEN
            RETURN;
        END IF;

        -- Selecting a better member for the current king promotes that member for the whole group.
        new_root := better;
    END IF;

    INSERT INTO duplicate_members (record_id, king_id)
    VALUES (new_root, new_root)
    ON CONFLICT (record_id) DO NOTHING;

    IF worse = worse_root THEN
        -- Explicitly assigning a king to a destination king moves its whole group.
        WITH moved AS (
            UPDATE duplicate_members SET king_id = new_root WHERE king_id = worse_root
                RETURNING record_id)
        SELECT array_agg(record_id)
        INTO moved_ids
        FROM moved;

        -- An isolated king has no stored membership yet.
        INSERT INTO duplicate_members (record_id, king_id)
        VALUES (worse, new_root)
        ON CONFLICT (record_id) DO NOTHING;
        moved_ids := COALESCE(moved_ids, ARRAY [worse]);

        -- Transferring a whole group also transfers its king rulings to the destination king.
        INSERT INTO alternative_records (lesser_record_id, greater_record_id)
        SELECT LEAST(new_root, other_id), GREATEST(new_root, other_id)
        FROM (SELECT CASE
                         WHEN lesser_record_id = worse_root THEN greater_record_id
                         ELSE lesser_record_id END AS other_id
              FROM alternative_records
              WHERE lesser_record_id = worse_root
                 OR greater_record_id = worse_root) rulings
        WHERE other_id <> new_root
        ON CONFLICT DO NOTHING;
        DELETE
        FROM alternative_records
        WHERE lesser_record_id = worse_root
           OR greater_record_id = worse_root;

        INSERT INTO unrelated_records (lesser_record_id, greater_record_id)
        SELECT LEAST(new_root, other_id), GREATEST(new_root, other_id)
        FROM (SELECT CASE
                         WHEN lesser_record_id = worse_root THEN greater_record_id
                         ELSE lesser_record_id END AS other_id
              FROM unrelated_records
              WHERE lesser_record_id = worse_root
                 OR greater_record_id = worse_root) rulings
        WHERE other_id <> new_root
        ON CONFLICT DO NOTHING;
        DELETE
        FROM unrelated_records
        WHERE lesser_record_id = worse_root
           OR greater_record_id = worse_root;
    ELSE
        -- Moving a subordinate changes only that record and drops its inherited rulings.
        SELECT array_agg(record_id)
        INTO affected_ids
        FROM (SELECT duplicate_group_members(worse_root) AS record_id
              UNION
              SELECT duplicate_group_members(better_root)) members;
        UPDATE duplicate_members SET king_id = new_root WHERE record_id = worse;
        moved_ids := ARRAY [worse];
    END IF;

    DELETE
    FROM hamming_distance
    WHERE (left_id = ANY (moved_ids) OR right_id = ANY (moved_ids))
      AND pair_is_ruled(left_id, right_id);

    IF affected_ids IS NOT NULL THEN
        INSERT INTO hamming_distance_queue (record_id)
        SELECT unnest(affected_ids)
        ON CONFLICT (record_id) DO NOTHING;
    END IF;
END;
$$ LANGUAGE plpgsql;
