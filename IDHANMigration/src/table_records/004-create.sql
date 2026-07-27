CREATE TABLE records
(
    record_id SERIAL PRIMARY KEY,
    sha256    bytea UNIQUE NOT NULL,
    CHECK ( LENGTH(sha256) = 32 )
);