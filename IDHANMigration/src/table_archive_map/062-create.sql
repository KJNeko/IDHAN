CREATE TABLE archive_map
(
    archive_id INTEGER REFERENCES archives (archive_id) NOT NULL,
    record_id  INTEGER REFERENCES records (record_id)   NOT NULL,
    UNIQUE (archive_id, record_id)
);
