CREATE TABLE mime
(
    mime_id        SERIAL PRIMARY KEY,
    name           TEXT UNIQUE NOT NULL,
    best_extension TEXT        NOT NULL
);