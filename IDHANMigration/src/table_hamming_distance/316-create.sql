CREATE TABLE hamming_distance
(
    left_id  INTEGER REFERENCES records (record_id),
    right_id INTEGER REFERENCES records (record_id),
    distance SMALLINT,
    PRIMARY KEY (left_id, right_id),
    CHECK (left_id < right_id),
    CHECK (distance <= 8)
);
