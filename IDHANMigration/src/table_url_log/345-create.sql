CREATE TABLE url_log
(
    url_id    INTEGER REFERENCES urls (url_id) PRIMARY KEY,
    log_time  TIMESTAMP,
    last_code SMALLINT
);