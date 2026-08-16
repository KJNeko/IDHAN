CREATE TABLE archive_metadata
(
    record_id      INTEGER REFERENCES records (record_id)   NOT NULL,
    archive_id     INTEGER REFERENCES archives (archive_id) NOT NULL,
    password_bytes BYTEA                                    NULL,
    encrypted      BOOLEAN DEFAULT FALSE                    NOT NULL,
    PRIMARY KEY (record_id)
);
