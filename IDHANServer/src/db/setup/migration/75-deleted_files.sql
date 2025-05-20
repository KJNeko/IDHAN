CREATE TABLE deleted_files
(
    record_id    INTEGER UNIQUE REFERENCES records (record_id) NOT NULL,
    deleted_time TIMESTAMP WITHOUT TIME ZONE                   NOT NULL DEFAULT CURRENT_TIMESTAMP
);