CREATE TABLE records
(
    record_id     SERIAL PRIMARY KEY,
    sha256        BYTEA UNIQUE                NOT NULL,
    creation_time TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP
);