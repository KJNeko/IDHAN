-- Each record has one king; kings point to themselves and isolated records may omit membership.
-- Relationship mutation functions maintain these assignments under the relationship lock.
CREATE TABLE duplicate_members
(
    record_id INTEGER PRIMARY KEY REFERENCES records (record_id) ON DELETE CASCADE,
    king_id   INTEGER NOT NULL REFERENCES records (record_id)
);
CREATE INDEX ON duplicate_members (king_id);
