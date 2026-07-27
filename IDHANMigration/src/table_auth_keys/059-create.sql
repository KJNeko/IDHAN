CREATE TABLE auth_keys
(
    key_id   SERIAL PRIMARY KEY,
    key_hash BYTEA UNIQUE NOT NULL CHECK ( length(key_hash) = 32 )
);