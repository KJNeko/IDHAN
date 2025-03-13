CREATE TABLE url_map
(
    record_id INTEGER,
    url_id    INTEGER REFERENCES urls (url_id),
    UNIQUE (record_id, url_id) -- Simply to prevent duplicates of the same thing
);