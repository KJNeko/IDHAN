-- A record is an alternative of exactly the records it was paired with, and of nothing those
-- records were in turn paired with. Stored with the lower id first so the primary key rejects the
-- same pair arriving in either order.
CREATE TABLE alternative_records
(
    lesser_record_id  INTEGER REFERENCES records (record_id) NOT NULL,
    greater_record_id INTEGER REFERENCES records (record_id) NOT NULL,
    PRIMARY KEY (lesser_record_id, greater_record_id),
    CHECK (lesser_record_id < greater_record_id)
);

CREATE INDEX ON alternative_records (greater_record_id);
