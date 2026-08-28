-- Existing acyclic chains can be converted safely. Refuse to guess a king for any legacy cycle.
DO
$$
    BEGIN
        IF (EXISTS(SELECT 1
                   FROM duplicate_pairs pair
                   WHERE NOT EXISTS (SELECT 1
                                     FROM flattened_duplicates flattened
                                     WHERE flattened.record_id = pair.worse_record_id))) THEN
            RAISE EXCEPTION 'Can''t convert cyclic duplicate relationships to duplicate groups';
        END IF;
    END;
$$;

CREATE TABLE duplicate_groups
(
    duplicate_id INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    king_id      INTEGER REFERENCES records (record_id) UNIQUE NOT NULL
);

CREATE TABLE duplicate_group_inferiors
(
    duplicate_id INTEGER REFERENCES duplicate_groups (duplicate_id) ON DELETE CASCADE NOT NULL,
    inferior_id  INTEGER REFERENCES records (record_id) UNIQUE                        NOT NULL,
    PRIMARY KEY (duplicate_id, inferior_id)
);

INSERT INTO duplicate_groups (king_id)
SELECT DISTINCT root_id
FROM flattened_duplicates
ORDER BY root_id;

INSERT INTO duplicate_group_inferiors (duplicate_id, inferior_id)
SELECT duplicate_group.duplicate_id, flattened.record_id
FROM flattened_duplicates flattened
         JOIN duplicate_groups duplicate_group ON duplicate_group.king_id = flattened.root_id
WHERE flattened.record_id != flattened.root_id;

-- Preserve the existing root lookup interface for distance and similarity queries.
CREATE OR REPLACE VIEW flattened_duplicates AS
SELECT king_id AS record_id, king_id AS root_id
FROM duplicate_groups
UNION
SELECT inferior.inferior_id, duplicate_group.king_id
FROM duplicate_group_inferiors inferior
         JOIN duplicate_groups duplicate_group USING (duplicate_id);

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
END;
$$ LANGUAGE plpgsql;

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

    RETURN TRUE;
END;
$$ LANGUAGE plpgsql;

DROP TABLE duplicate_pairs;
