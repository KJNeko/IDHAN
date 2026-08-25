-- Pairs a human has judged to be coincidental lookalikes. Stored with the lower id first so the
-- primary key rejects the same pair arriving in either order.
CREATE TABLE unrelated_records
(
    lesser_record_id  INTEGER REFERENCES records (record_id) NOT NULL,
    greater_record_id INTEGER REFERENCES records (record_id) NOT NULL,
    PRIMARY KEY (lesser_record_id, greater_record_id),
    CHECK (lesser_record_id < greater_record_id)
);

CREATE INDEX ON unrelated_records (greater_record_id);
