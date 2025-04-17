CREATE TABLE url_map
(
    record_id INTEGER REFERENCES records (record_id) NOT NULL,
    url_id    INTEGER REFERENCES urls (url_id)       NOT NULL,
    UNIQUE (record_id, url_id) -- Simply to prevent duplicates of the same thing
);