CREATE TABLE metadata
(
    record_id        INTEGER REFERENCES records (record_id) NOT NULL,
    simple_mime_type SMALLINT                               NOT NULL,
    json             JSON,
    UNIQUE (record_id)
);