DROP TABLE IF EXISTS archive_metadata; -- Drop old table

CREATE TABLE archive_metadata
(
    record_id      INTEGER REFERENCES records (record_id) UNIQUE NOT NULL,
    archive_id     INTEGER REFERENCES archives (archive_id)      NOT NULL,
    password_bytes BYTEA                                         NULL
);