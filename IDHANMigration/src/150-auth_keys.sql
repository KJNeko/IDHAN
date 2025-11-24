CREATE TABLE auth_keys
(
    key_id   SERIAL PRIMARY KEY,
    key_hash BYTEA UNIQUE NOT NULL CHECK ( length(key_hash) = 32 )
);

INSERT INTO auth_keys (key_hash)
VALUES (digest('cunny', 'sha256'));