CREATE TABLE file_notes
(
    record_id INTEGER UNIQUE REFERENCES records (record_id),
    note TEXT NOT NULL,
    PRIMARY KEY (record_id)
);