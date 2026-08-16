CREATE TABLE label_mappings (
    note_id INTEGER REFERENCES notes(note_id),
    label_id INTEGER REFERENCES note_labels(label_id),
    UNIQUE(note_id, label_id)
);