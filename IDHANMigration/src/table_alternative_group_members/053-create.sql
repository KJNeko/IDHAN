CREATE TABLE alternative_group_members
(
    group_id  INTEGER REFERENCES alternative_groups (group_id) NOT NULL,
    record_id INTEGER REFERENCES records (record_id) UNIQUE    NOT NULL,
    UNIQUE (group_id, record_id)
);

CREATE INDEX ON alternative_group_members (group_id);
