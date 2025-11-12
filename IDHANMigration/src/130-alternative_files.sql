CREATE TABLE alternative_groups
(
    group_id SERIAL PRIMARY KEY NOT NULL
);

CREATE TABLE alternative_group_members
(
    group_id  INTEGER REFERENCES alternative_groups (group_id) NOT NULL,
    record_id INTEGER REFERENCES records (record_id) UNIQUE    NOT NULL,
    UNIQUE (group_id, record_id)
);

CREATE INDEX ON alternative_group_members (group_id);

CREATE TABLE duplicate_pairs
(
    worse_record_id  INTEGER REFERENCES records (record_id) UNIQUE NOT NULL,
    better_record_id INTEGER REFERENCES records (record_id)        NOT NULL,
    CHECK (worse_record_id != better_record_id)
);

CREATE INDEX ON duplicate_pairs (better_record_id);

CREATE OR REPLACE FUNCTION insert_duplicate_pair(
    worse INTEGER,
    better INTEGER
) RETURNS VOID AS
$$
BEGIN

    IF (EXISTS(SELECT 1 FROM duplicate_pairs WHERE worse_record_id = worse AND better_record_id = better)) THEN
        RETURN;
    END IF;

    -- check that the worse id isn't being added again
    IF (EXISTS(SELECT 1 FROM duplicate_pairs WHERE worse_record_id = worse)) THEN
        RAISE EXCEPTION 'Can''t insert an already inserted worse record';
    END IF;

    -- Check that the worse record wouldn't make a cyclic chain
    IF (EXISTS(SELECT 1 FROM duplicate_pairs WHERE better_record_id = worse AND worse_record_id = better)) THEN
        RAISE EXCEPTION 'Inserting this duplicate pair would result in a cyclic chain';
    end if;

    INSERT INTO duplicate_pairs (worse_record_id, better_record_id)
    VALUES (worse, better);

    UPDATE duplicate_pairs
    SET better_record_id = better
    WHERE better_record_id = worse;
END;

$$ LANGUAGE plpgsql;
