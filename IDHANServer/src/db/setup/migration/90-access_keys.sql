CREATE TABLE access_keys
(
    access_key_id SERIAL PRIMARY KEY,
    access_key    BYTEA UNIQUE NOT NULL,
    permissions   INT          NOT NULL DEFAULT 0
);
