CREATE TABLE tag_namespaces
(
    namespace_id   SERIAL PRIMARY KEY,
    namespace_text TEXT UNIQUE NOT NULL,
    color          bytea,
    CHECK (color IS NULL OR OCTET_LENGTH(color) == 3)
);