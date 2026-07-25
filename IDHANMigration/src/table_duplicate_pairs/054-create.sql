CREATE TABLE duplicate_pairs
(
    worse_record_id  INTEGER REFERENCES records (record_id) UNIQUE NOT NULL,
    better_record_id INTEGER REFERENCES records (record_id)        NOT NULL,
    CHECK (worse_record_id != better_record_id)
);

CREATE INDEX ON duplicate_pairs (better_record_id);
