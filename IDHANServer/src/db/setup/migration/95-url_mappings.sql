CREATE TABLE url_mappings
(
    record_id INTEGER REFERENCES records (record_id),
    url_id    INTEGER REFERENCES urls (url_id),
    PRIMARY KEY (record_id, url_id)
);