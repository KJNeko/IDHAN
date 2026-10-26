-- Transfer rulings to the merged king and delete distances within the group and to groups
-- covered by those rulings.
CREATE OR REPLACE FUNCTION insert_duplicate_pair(
    worse INTEGER,
    better INTEGER
) RETURNS VOID AS
$$
DECLARE
    worse_duplicate_id  INTEGER;
    better_duplicate_id INTEGER;
    target_duplicate_id INTEGER;
    member_ids          INTEGER[];
    inferior_ids        INTEGER[];
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);

    SELECT duplicate_id
    INTO worse_duplicate_id
    FROM (SELECT duplicate_id
          FROM duplicate_groups
          WHERE king_id = worse
          UNION
          SELECT duplicate_id
          FROM duplicate_group_inferiors
          WHERE inferior_id = worse) duplicate;

    SELECT duplicate_id
    INTO better_duplicate_id
    FROM (SELECT duplicate_id
          FROM duplicate_groups
          WHERE king_id = better
          UNION
          SELECT duplicate_id
          FROM duplicate_group_inferiors
          WHERE inferior_id = better) duplicate;

    SELECT array_agg(record_id)
    INTO member_ids
    FROM (SELECT king_id AS record_id
          FROM duplicate_groups
          WHERE duplicate_id IN (worse_duplicate_id, better_duplicate_id)
          UNION
          SELECT inferior_id
          FROM duplicate_group_inferiors
          WHERE duplicate_id IN (worse_duplicate_id, better_duplicate_id)
          UNION
          SELECT worse
          UNION
          SELECT better) members;

    inferior_ids := array_remove(member_ids, better);

    target_duplicate_id := COALESCE(better_duplicate_id, worse_duplicate_id);

    IF target_duplicate_id IS NULL THEN
        INSERT INTO duplicate_groups (king_id)
        VALUES (better)
        RETURNING duplicate_id INTO target_duplicate_id;
    ELSE
        DELETE
        FROM duplicate_group_inferiors
        WHERE duplicate_id IN (worse_duplicate_id, better_duplicate_id);

        DELETE
        FROM duplicate_groups
        WHERE duplicate_id IN (worse_duplicate_id, better_duplicate_id)
          AND duplicate_id != target_duplicate_id;

        UPDATE duplicate_groups
        SET king_id = better
        WHERE duplicate_id = target_duplicate_id;
    END IF;

    INSERT INTO duplicate_group_inferiors (duplicate_id, inferior_id)
    SELECT target_duplicate_id, member.record_id
    FROM unnest(member_ids) AS member(record_id)
    WHERE member.record_id != better;

    INSERT INTO alternative_records (lesser_record_id, greater_record_id)
    SELECT DISTINCT LEAST(better, other.record_id), GREATEST(better, other.record_id)
    FROM (SELECT greater_record_id AS record_id
          FROM alternative_records
          WHERE lesser_record_id = ANY (inferior_ids)
          UNION
          SELECT lesser_record_id
          FROM alternative_records
          WHERE greater_record_id = ANY (inferior_ids)) other
    WHERE other.record_id <> ALL (member_ids)
    ON CONFLICT DO NOTHING;

    INSERT INTO unrelated_records (lesser_record_id, greater_record_id)
    SELECT DISTINCT LEAST(better, other.record_id), GREATEST(better, other.record_id)
    FROM (SELECT greater_record_id AS record_id
          FROM unrelated_records
          WHERE lesser_record_id = ANY (inferior_ids)
          UNION
          SELECT lesser_record_id
          FROM unrelated_records
          WHERE greater_record_id = ANY (inferior_ids)) other
    WHERE other.record_id <> ALL (member_ids)
    ON CONFLICT DO NOTHING;

    DELETE
    FROM alternative_records
    WHERE lesser_record_id = ANY (inferior_ids)
       OR greater_record_id = ANY (inferior_ids);

    DELETE
    FROM unrelated_records
    WHERE lesser_record_id = ANY (inferior_ids)
       OR greater_record_id = ANY (inferior_ids);

    DELETE
    FROM hamming_distance
    WHERE left_id = ANY (member_ids)
      AND right_id = ANY (member_ids);

    DELETE
    FROM hamming_distance hamming
        USING unnest(member_ids) AS member(record_id),
        (SELECT greater_record_id AS king_id
         FROM alternative_records
         WHERE lesser_record_id = better
         UNION
         SELECT lesser_record_id
         FROM alternative_records
         WHERE greater_record_id = better
         UNION
         SELECT greater_record_id
         FROM unrelated_records
         WHERE lesser_record_id = better
         UNION
         SELECT lesser_record_id
         FROM unrelated_records
         WHERE greater_record_id = better) ruled,
        LATERAL duplicate_group_members(ruled.king_id) AS other(record_id)
    WHERE hamming.left_id = LEAST(member.record_id, other.record_id)
      AND hamming.right_id = GREATEST(member.record_id, other.record_id);
END;
$$ LANGUAGE plpgsql;
