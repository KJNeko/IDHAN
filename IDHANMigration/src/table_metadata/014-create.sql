CREATE TABLE metadata
(
    record_id        INTEGER REFERENCES records (record_id) NOT NULL,
    simple_mime_type SMALLINT                               NOT NULL,
    json             json DEFAULT NULL,
    PRIMARY KEY (record_id)
);
