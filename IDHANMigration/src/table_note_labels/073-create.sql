CREATE TABLE note_labels
(
    label_id SERIAL PRIMARY KEY,
    label    TEXT UNIQUE
);