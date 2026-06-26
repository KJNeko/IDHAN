CREATE TABLE record_notes
(
    record_id INTEGER REFERENCES records (record_id),
    note_id   INTEGER REFERENCES notes (note_id),
    UNIQUE(record_id, note_id)
);