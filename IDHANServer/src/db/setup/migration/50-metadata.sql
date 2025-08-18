CREATE TABLE metadata
(
    record_id        recordid REFERENCES records (record_id) NOT NULL,
    simple_mime_type SMALLINT                                NOT NULL,
    json             json DEFAULT NULL,
    UNIQUE (record_id)
);