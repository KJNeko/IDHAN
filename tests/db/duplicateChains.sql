-- Executed only inside a migrated scratch schema by duplicateChains.cpp.
SET plpgsql.check_asserts = on;
DO $$
DECLARE
    ids INTEGER[];
    a INTEGER; b INTEGER; c INTEGER; d INTEGER;
    e INTEGER; f INTEGER; g INTEGER; h INTEGER;
BEGIN
    SELECT array_agg(record_id ORDER BY record_id) INTO ids
    FROM (SELECT record_id FROM records ORDER BY record_id LIMIT 8) fixtures;
    ASSERT cardinality(ids) = 8, 'Eight fixture records are required';
    a := ids[1]; b := ids[2]; c := ids[3]; d := ids[4];
    e := ids[5]; f := ids[6]; g := ids[7]; h := ids[8];
    TRUNCATE duplicate_pairs, alternative_records, unrelated_records,
             hamming_distance, hamming_distance_queue;

    PERFORM insert_duplicate_pair(b, a);
    PERFORM insert_duplicate_pair(c, b);
    PERFORM insert_duplicate_pair(d, c);
    ASSERT (SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = c) = b,
        'Adding to an inferior must preserve its ancestors';
    ASSERT duplicate_king(d) = a, 'The recursive view must resolve deep descendants';
    ASSERT NOT remove_duplicate_pair(d, a), 'Clearing a transitive relationship must not erase a direct edge';
    ASSERT remove_duplicate_pair(b, c), 'Clearing an edge must work in either argument order';
    ASSERT duplicate_king(b) = a AND duplicate_king(d) = c, 'A middle cut must leave both chains intact';
    ASSERT (SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = d) = c,
        'A detached subtree must preserve its ordering';
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 4, 'Every affected member must be requeued';

    -- Reverse a direct judgment and then a judgment across several hops.
    PERFORM insert_duplicate_pair(a, b);
    ASSERT duplicate_king(a) = b AND duplicate_king(b) = b, 'Reversing an edge must not create a cycle';
    PERFORM insert_duplicate_pair(c, a);
    PERFORM insert_duplicate_pair(b, d);
    ASSERT duplicate_king(a) = d AND duplicate_king(b) = d AND duplicate_king(c) = d,
        'A deep reversal must retain all records in an acyclic chain';
    ASSERT (SELECT count(*) FROM flattened_duplicates) = 4, 'No record may disappear into a cycle';

    TRUNCATE duplicate_pairs, alternative_records, unrelated_records,
             hamming_distance, hamming_distance_queue;
    PERFORM insert_duplicate_pair(b, a);
    PERFORM insert_duplicate_pair(c, b);
    PERFORM insert_duplicate_pair(f, e);
    INSERT INTO alternative_records VALUES (LEAST(a, g), GREATEST(a, g));
    PERFORM insert_duplicate_pair(b, f);
    ASSERT duplicate_king(c) = e AND duplicate_king(a) = a, 'Reparenting moves the subtree, not its old ancestors';
    ASSERT (SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = b) = f,
        'The selected superior must remain the immediate parent';
    ASSERT pair_is_ruled(a, g) AND NOT pair_is_ruled(c, g), 'A moved branch must leave inherited rulings behind';
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 5, 'Reparenting must restore suppressed candidates';

    TRUNCATE duplicate_pairs, alternative_records, unrelated_records,
             hamming_distance, hamming_distance_queue;
    PERFORM insert_duplicate_pair(b, a);
    PERFORM insert_duplicate_pair(d, c);
    INSERT INTO hamming_distance VALUES (LEAST(b,d), GREATEST(b,d), 0);
    INSERT INTO alternative_records VALUES (LEAST(b,d), GREATEST(b,d));
    ASSERT (SELECT count(*) FROM hamming_distance) = 0, 'A ruling must discard inferior-to-inferior distances';
    ASSERT pair_is_ruled(b,d), 'A ruling must cover both entire chains';
    DELETE FROM alternative_records;
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 4, 'Clearing alternatives must queue all members';
    ASSERT NOT pair_is_ruled(b,d), 'Cleared alternatives must stop being inherited';

    TRUNCATE hamming_distance_queue;
    INSERT INTO unrelated_records VALUES (LEAST(b,d), GREATEST(b,d));
    DELETE FROM unrelated_records;
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 4, 'Clearing unrelated rulings must queue all members';

    -- A newly inferior root passes its rulings to the root derived from the combined chain.
    INSERT INTO alternative_records VALUES (LEAST(a,e), GREATEST(a,e));
    INSERT INTO unrelated_records VALUES (LEAST(a,h), GREATEST(a,h));
    INSERT INTO hamming_distance VALUES (LEAST(b,e), GREATEST(b,e), 1), (LEAST(b,h), GREATEST(b,h), 1);
    PERFORM insert_duplicate_pair(a, d);
    ASSERT duplicate_king(b) = c, 'Joining roots must preserve the selected superior ancestors';
    ASSERT (SELECT count(*) FROM alternative_records WHERE lesser_record_id = LEAST(c,e)
            AND greater_record_id = GREATEST(c,e)) = 1, 'Alternatives must follow a root that becomes inferior';
    ASSERT pair_is_ruled(b,e) AND pair_is_ruled(b,h), 'Moved rulings must remain inherited';
    ASSERT (SELECT count(*) FROM hamming_distance) = 0, 'Moving rulings must discard all covered distances';
    ASSERT NOT EXISTS (SELECT 1 FROM alternative_records
                       WHERE lesser_record_id <> duplicate_king(lesser_record_id)
                          OR greater_record_id <> duplicate_king(greater_record_id)), 'Rulings must name derived roots';

    TRUNCATE hamming_distance_queue;
    ASSERT remove_duplicate_pair(a,d), 'A root merge must remain a removable direct link';
    ASSERT duplicate_king(b) = a AND duplicate_king(d) = c, 'Clearing the merge must restore both original chains';
    ASSERT NOT pair_is_ruled(b,e) AND pair_is_ruled(d,e), 'Rulings stay with the previous root after a split';
    ASSERT (SELECT count(*) FROM hamming_distance_queue) = 4, 'Splitting a merged chain queues every member';
END;
$$;
