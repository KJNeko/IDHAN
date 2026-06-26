CREATE TABLE notes
(
    note_id SERIAL PRIMARY KEY,
    note    TEXT UNIQUE NOT NULL
);