CREATE TABLE access_keys
(
    access_key_id SERIAL PRIMARY KEY,
    access_key bytea UNIQUE NOT NULL,
    permissions   INT          NOT NULL DEFAULT 0
);
