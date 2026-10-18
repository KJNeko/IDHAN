-- Executed only inside a migrated scratch schema by duplicateChains.cpp.
SET plpgsql.check_asserts = on;
DO $$
DECLARE
    ids INTEGER[];
    a INTEGER; b INTEGER; c INTEGER; d INTEGER;
    f INTEGER; g INTEGER; o INTEGER; h INTEGER;
BEGIN
    SELECT array_agg(record_id ORDER BY record_id) INTO ids
    FROM (SELECT record_id FROM records ORDER BY record_id LIMIT 8) fixtures;
    ASSERT cardinality(ids) = 8, 'Eight fixture records are required';
    a := ids[1]; b := ids[2]; c := ids[3]; d := ids[4];
    f := ids[5]; g := ids[6]; o := ids[7]; h := ids[8];
    TRUNCATE duplicate_members, alternative_records, unrelated_records,
             hamming_distance, hamming_distance_queue;

    PERFORM insert_duplicate_pair(ARRAY [b, c, d], ARRAY [a, b, c]);
    ASSERT duplicate_king(b) = a AND duplicate_king(c) = a AND duplicate_king(d) = a,
        'Comparisons through subordinates must flatten directly to the existing king';
    ASSERT (SELECT count(*) FROM duplicate_group_members(a)) = 4,
        'Adding members must retain the receiving group';
    PERFORM insert_duplicate_pair(c, b);
    ASSERT duplicate_king(c) = a, 'Comparisons between subordinates must leave the king unchanged';
    ASSERT NOT remove_duplicate_pair(b, c), 'Subordinates have no stored relationship to each other';
    ASSERT remove_duplicate_pair(a, b), 'Removing a member must work in either argument order';
    ASSERT duplicate_king(a) = a AND duplicate_king(b) = b
        AND duplicate_king(c) = a AND duplicate_king(d) = a,
        'Removing a member must retain the king and the other flat members';
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 4,
        'Detaching a member must requeue the affected group';
    ASSERT NOT remove_duplicate_pair(a, b), 'An already detached member must not be removed again';
    ASSERT NOT remove_duplicate_pair(a, a), 'A king cannot be detached from itself';

    -- Moving a subordinate changes only that record, regardless of how it originally joined.
    PERFORM insert_duplicate_pair(f, g);
    TRUNCATE hamming_distance_queue;
    INSERT INTO alternative_records VALUES (LEAST(a, h), GREATEST(a, h));
    PERFORM insert_duplicate_pair(c, f);
    ASSERT duplicate_king(c) = g AND duplicate_king(d) = a AND duplicate_king(a) = a,
        'Moving a subordinate must not move records previously compared through it';
    ASSERT pair_is_ruled(d, h) AND NOT pair_is_ruled(c, h),
        'A moved subordinate must leave its old king rulings behind';
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 5,
        'Moving a subordinate between groups must requeue both groups';

    -- A failed batch rolls back earlier membership changes.
    BEGIN
        PERFORM insert_duplicate_pair(ARRAY [a, o], ARRAY [g, 999999999]);
        ASSERT FALSE, 'An invalid pair must fail the batch';
    EXCEPTION
        WHEN foreign_key_violation THEN
            NULL;
    END;
    ASSERT duplicate_king(a) = a AND duplicate_king(d) = a,
        'A failed batch must restore all earlier group assignments';
END;
$$;

DO
$$
    DECLARE
        ids INTEGER[];
        a   INTEGER; b INTEGER; c INTEGER; d INTEGER;
        f   INTEGER; g INTEGER; o INTEGER; h INTEGER;
    BEGIN
        SELECT array_agg(record_id ORDER BY record_id)
        INTO ids
        FROM (SELECT record_id FROM records ORDER BY record_id LIMIT 8) fixtures;
        a := ids[1]; b := ids[2]; c := ids[3]; d := ids[4];
        f := ids[5]; g := ids[6]; o := ids[7]; h := ids[8];
        TRUNCATE duplicate_members, alternative_records, unrelated_records,
            hamming_distance, hamming_distance_queue;

        PERFORM insert_duplicate_pair(ARRAY [a, b, c, d], ARRAY [f, f, g, g]);
        INSERT INTO alternative_records VALUES (LEAST(f, h), GREATEST(f, h)), (LEAST(g, h), GREATEST(g, h));
        INSERT INTO unrelated_records VALUES (LEAST(f, g), GREATEST(f, g));
        INSERT INTO hamming_distance VALUES (LEAST(a, o), GREATEST(a, o), 0), (LEAST(c, o), GREATEST(c, o), 0);
        PERFORM insert_duplicate_pair(f, o);
        PERFORM insert_duplicate_pair(g, o);
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = o) = 7,
            'Successive king merges must accumulate all members and both former kings';
        ASSERT (SELECT count(*) FROM alternative_records) = 1 AND pair_is_ruled(a, h) AND pair_is_ruled(c, h),
            'Shared external rulings must transfer once to the receiving king';
        ASSERT NOT EXISTS (SELECT 1 FROM unrelated_records), 'Rulings between merged groups must disappear';
        ASSERT NOT EXISTS (SELECT 1 FROM hamming_distance), 'Merges must discard newly ruled distances';

        -- Former kings are ordinary members: detaching one cannot restore its old group.
        ASSERT remove_duplicate_pair(f, o), 'A former king must be detachable as one ordinary member';
        ASSERT duplicate_king(f) = f AND duplicate_king(a) = o AND duplicate_king(b) = o,
            'Detaching a former king must leave its former members with their current king';
        ASSERT pair_is_ruled(a, h) AND NOT pair_is_ruled(f, h), 'Rulings must stay with the existing king';

        PERFORM insert_duplicate_pair(o, b);
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = b) = 6 AND pair_is_ruled(o, h),
            'Selecting a better member for the king must promote it for the entire group';
        ASSERT duplicate_king(f) = f, 'Promoting a king must leave detached records alone';
    ASSERT NOT EXISTS (SELECT 1 FROM alternative_records
                       WHERE lesser_record_id <> duplicate_king(lesser_record_id)
                          OR greater_record_id <> duplicate_king(greater_record_id)),
        'Transferred rulings must reference kings directly';

        -- Rulings through members cover both groups and clearing them restores all candidates.
        PERFORM insert_duplicate_pair(f, h);
        INSERT INTO unrelated_records VALUES (LEAST(a, f), GREATEST(a, f));
        DELETE FROM alternative_records;
    TRUNCATE hamming_distance_queue;
        DELETE FROM unrelated_records;
        ASSERT (SELECT count(*) FROM hamming_distance_queue) = 8,
            'Clearing a ruling must requeue every member of both groups';
    END;
$$;

-- Mixed mutations must keep every assignment flat and remove all newly ruled candidates.
DO
$$
    DECLARE
        ids  INTEGER[];
        step INTEGER;
        a    INTEGER;
        b    INTEGER;
    BEGIN
        TRUNCATE duplicate_members, alternative_records, unrelated_records,
            hamming_distance, hamming_distance_queue;
        SELECT array_agg(record_id ORDER BY record_id)
        INTO ids
        FROM (SELECT record_id FROM records ORDER BY record_id LIMIT 8) fixtures;

        FOR step IN 1..200
            LOOP
                a := ids[1 + (step * 17 + step / 3) % 8];
                b := ids[1 + (step * 11 + step / 5) % 8];
                IF a = b THEN
                    CONTINUE;
                END IF;

                INSERT INTO hamming_distance (left_id, right_id, distance)
                SELECT left_id, right_id, 0
                FROM unnest(ids) AS left_member(left_id)
                         CROSS JOIN unnest(ids) AS right_member(right_id)
                WHERE left_id < right_id
                  AND NOT pair_is_ruled(left_id, right_id)
                ON CONFLICT DO NOTHING;

                IF step % 5 = 0 THEN
                    PERFORM remove_duplicate_pair(a, b);
                ELSE
                    PERFORM insert_duplicate_pair(a, b);
                END IF;

                IF step % 9 = 0 THEN
                    INSERT INTO alternative_records VALUES (LEAST(a, b), GREATEST(a, b)) ON CONFLICT DO NOTHING;
                END IF;
                IF step % 11 = 0 THEN
                    INSERT INTO unrelated_records VALUES (LEAST(a, b), GREATEST(a, b)) ON CONFLICT DO NOTHING;
                END IF;

                ASSERT NOT EXISTS (SELECT 1
                                   FROM duplicate_members member
                                            LEFT JOIN duplicate_members king ON king.record_id = member.king_id
                                   WHERE king.king_id IS DISTINCT FROM member.king_id),
                    'Every member must point directly to a king that points to itself';
                ASSERT NOT EXISTS (SELECT 1 FROM hamming_distance WHERE pair_is_ruled(left_id, right_id)),
                    'Mutations must discard distances covered by the resulting groups and rulings';
            END LOOP;
    END;
$$;

-- A long sequence of comparisons must still produce one flat group.
DO
$$
    DECLARE
        ids                INTEGER[];
        original_positions TEXT[];
    BEGIN
        TRUNCATE duplicate_members, alternative_records, unrelated_records,
            hamming_distance, hamming_distance_queue;
        INSERT INTO records (sha256)
        SELECT decode(lpad(to_hex(seed), 64, '0'), 'hex')
        FROM generate_series(1, 1025) AS seed
        ON CONFLICT (sha256) DO NOTHING;
        SELECT array_agg(record_id ORDER BY record_id)
        INTO ids
        FROM (SELECT record_id FROM records ORDER BY record_id LIMIT 1025) fixtures;

        PERFORM insert_duplicate_pair(ids[2:1024], ids[1:1023]);
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = ids[1]) = 1024,
            'Comparing each new record to the previous record must keep all memberships flat';
        ASSERT duplicate_king(ids[1025]) = ids[1025], 'An isolated record is its own king';
        ASSERT (SELECT count(*) FROM duplicate_group_members(ids[1025])) = 1,
            'An isolated group contains its king';

        ASSERT remove_duplicate_pair(ids[512], ids[1]), 'Any subordinate can be detached from its king';
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = ids[1]) = 1023
            AND duplicate_king(ids[512]) = ids[512] AND duplicate_king(ids[513]) = ids[1],
            'Detaching a formerly intermediate record must leave every other member in place';
        PERFORM insert_duplicate_pair(ids[512], ids[1]);
        PERFORM insert_duplicate_pair(ids[1], ids[1024]);
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = ids[1024]) = 1024,
            'Replacing the king must update every member directly';

        SELECT array_agg(ctid::TEXT ORDER BY record_id)
        INTO original_positions
        FROM duplicate_members
        WHERE king_id = ids[1024];
        PERFORM insert_duplicate_pair(ids[1025], ids[1024]);
        ASSERT original_positions = (SELECT array_agg(ctid::TEXT ORDER BY record_id)
                                     FROM duplicate_members
                                     WHERE king_id = ids[1024]
                                       AND record_id <> ids[1025]),
            'Adding an inferior must not rewrite the receiving group';
        ASSERT (SELECT count(*) FROM duplicate_members WHERE king_id = ids[1024]) = 1025,
            'The membership table must contain the king and every subordinate';
END;
$$;
