CREATE OR REPLACE FUNCTION insert_duplicate_pair(worse_ids INTEGER[], better_ids INTEGER[]) RETURNS VOID AS
$$
DECLARE
    pair_index INTEGER;
BEGIN
    IF worse_ids IS NULL OR better_ids IS NULL
        OR cardinality(worse_ids) <> cardinality(better_ids)
        OR array_position(worse_ids, NULL) IS NOT NULL
        OR array_position(better_ids, NULL) IS NOT NULL THEN
        RAISE EXCEPTION 'Duplicate pair arrays must have the same length and contain no nulls';
    END IF;

    FOR pair_index IN 1..cardinality(worse_ids)
        LOOP
            PERFORM insert_duplicate_pair(worse_ids[pair_index], better_ids[pair_index]);
        END LOOP;
END;
$$ LANGUAGE plpgsql;
