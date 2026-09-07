-- Restore direct comparison edges. Existing inferiors retain their recorded king because the
-- flat schema discarded intermediate ordering.
CREATE TABLE duplicate_pairs
(
    worse_record_id INTEGER PRIMARY KEY REFERENCES records (record_id),
    better_record_id INTEGER NOT NULL REFERENCES records (record_id),
    CHECK (worse_record_id <> better_record_id)
);
CREATE INDEX ON duplicate_pairs (better_record_id);

INSERT INTO duplicate_pairs (worse_record_id, better_record_id)
SELECT inferior.inferior_id, duplicate_group.king_id
FROM duplicate_group_inferiors inferior
JOIN duplicate_groups duplicate_group USING (duplicate_id);

-- Derive roots recursively. UNION terminates traversal of legacy cycles.
CREATE OR REPLACE VIEW flattened_duplicates AS
WITH RECURSIVE chain(record_id, root_id) AS (
    SELECT pair.better_record_id, pair.better_record_id
    FROM duplicate_pairs pair
    WHERE NOT EXISTS (SELECT 1 FROM duplicate_pairs parent
                      WHERE parent.worse_record_id = pair.better_record_id)
    UNION
    SELECT pair.worse_record_id, chain.root_id
    FROM duplicate_pairs pair JOIN chain ON pair.better_record_id = chain.record_id
)
SELECT record_id, root_id FROM chain;

CREATE OR REPLACE FUNCTION duplicate_king(record INTEGER) RETURNS INTEGER AS
$$
SELECT COALESCE((SELECT root_id FROM flattened_duplicates WHERE record_id = record), record);
$$ LANGUAGE sql STABLE;

CREATE OR REPLACE FUNCTION duplicate_group_members(king INTEGER) RETURNS SETOF INTEGER AS
$$
SELECT king
UNION
SELECT record_id FROM flattened_duplicates WHERE root_id = king;
$$ LANGUAGE sql STABLE;

DROP TABLE duplicate_group_inferiors;
DROP TABLE duplicate_groups;

-- Preserve the superior's ancestors. If it is a descendant of the inferior, move it to the
-- inferior's former position before linking to avoid a cycle.
CREATE OR REPLACE FUNCTION insert_duplicate_pair(worse INTEGER, better INTEGER) RETURNS VOID AS
$$
DECLARE
    member_ids INTEGER[];
    worse_root INTEGER;
    better_root INTEGER;
    previous_parent INTEGER;
    would_cycle BOOLEAN;
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);
    IF worse = better THEN
        RETURN;
    END IF;

    worse_root := duplicate_king(worse);
    better_root := duplicate_king(better);
    SELECT array_agg(record_id) INTO member_ids
    FROM (SELECT duplicate_group_members(worse_root) AS record_id
          UNION SELECT duplicate_group_members(better_root)) members;
    SELECT better_record_id INTO previous_parent FROM duplicate_pairs WHERE worse_record_id = worse;

    WITH RECURSIVE descendants(record_id) AS (
        SELECT worse
        UNION ALL
        SELECT pair.worse_record_id FROM duplicate_pairs pair
        JOIN descendants ON pair.better_record_id = descendants.record_id
    )
    SELECT EXISTS (SELECT 1 FROM descendants WHERE record_id = better) INTO would_cycle;

    IF would_cycle THEN
        DELETE FROM duplicate_pairs WHERE worse_record_id = better;
        IF previous_parent IS NOT NULL THEN
            INSERT INTO duplicate_pairs (worse_record_id, better_record_id) VALUES (better, previous_parent);
        END IF;
    END IF;

    INSERT INTO duplicate_pairs (worse_record_id, better_record_id) VALUES (worse, better)
    ON CONFLICT (worse_record_id) DO UPDATE SET better_record_id = excluded.better_record_id;

    -- Transfer rulings when a root becomes inferior. Detached branches do not inherit the
    -- previous root's rulings.
    INSERT INTO alternative_records (lesser_record_id, greater_record_id)
    SELECT DISTINCT LEAST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id)),
                    GREATEST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id))
    FROM alternative_records
    WHERE (lesser_record_id = ANY(member_ids) OR greater_record_id = ANY(member_ids))
      AND duplicate_king(lesser_record_id) <> duplicate_king(greater_record_id)
    ON CONFLICT DO NOTHING;

    DELETE FROM alternative_records
    WHERE (lesser_record_id = ANY(member_ids) OR greater_record_id = ANY(member_ids))
      AND (lesser_record_id <> duplicate_king(lesser_record_id)
           OR greater_record_id <> duplicate_king(greater_record_id));

    INSERT INTO unrelated_records (lesser_record_id, greater_record_id)
    SELECT DISTINCT LEAST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id)),
                    GREATEST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id))
    FROM unrelated_records
    WHERE (lesser_record_id = ANY(member_ids) OR greater_record_id = ANY(member_ids))
      AND duplicate_king(lesser_record_id) <> duplicate_king(greater_record_id)
    ON CONFLICT DO NOTHING;

    DELETE FROM unrelated_records
    WHERE (lesser_record_id = ANY(member_ids) OR greater_record_id = ANY(member_ids))
      AND (lesser_record_id <> duplicate_king(lesser_record_id)
           OR greater_record_id <> duplicate_king(greater_record_id));

    DELETE FROM hamming_distance
    WHERE (left_id = ANY(member_ids) OR right_id = ANY(member_ids))
      AND pair_is_ruled(left_id, right_id);

    -- Reparenting between chains also clears inherited relationships for the detached branch.
    IF previous_parent IS NOT NULL AND worse_root <> better_root THEN
        INSERT INTO hamming_distance_queue (record_id)
        SELECT unnest(member_ids)
        ON CONFLICT (record_id) DO NOTHING;
    END IF;
END;
$$ LANGUAGE plpgsql;

-- Remove the selected edge; the recursive view derives roots for both resulting chains.
CREATE OR REPLACE FUNCTION remove_duplicate_pair(record_a INTEGER, record_b INTEGER)
RETURNS BOOLEAN AS
$$
DECLARE
    affected_ids INTEGER[];
    removed INTEGER;
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);
    SELECT array_agg(record_id) INTO affected_ids
    FROM duplicate_group_members(duplicate_king(record_a)) AS member(record_id);

    DELETE FROM duplicate_pairs
    WHERE (worse_record_id = record_a AND better_record_id = record_b)
       OR (worse_record_id = record_b AND better_record_id = record_a)
    RETURNING worse_record_id INTO removed;

    IF removed IS NULL THEN
        RETURN FALSE;
    END IF;

    -- Rulings stay with the previous root; every detached member must be reconsidered.
    INSERT INTO hamming_distance_queue (record_id)
    SELECT unnest(affected_ids)
    ON CONFLICT (record_id) DO NOTHING;
    RETURN TRUE;
END;
$$ LANGUAGE plpgsql;
