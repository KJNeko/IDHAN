CREATE TABLE tag_namespaces
(
    namespace_id   SERIAL PRIMARY KEY,
    namespace_text TEXT UNIQUE NOT NULL
);