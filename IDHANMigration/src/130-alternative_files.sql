CREATE TABLE alternative_groups
(
    group_id SERIAL PRIMARY KEY NOT NULL
);

CREATE TABLE alternative_group_members
(
    group_id  INTEGER REFERENCES alternative_groups (group_id),
    record_id INTEGER REFERENCES records (record_id)
);

CREATE TABLE alternative_pairs
(
    worse_record_id  INTEGER REFERENCES records (record_id),
    better_record_id INTEGER REFERENCES records (record_id)
);