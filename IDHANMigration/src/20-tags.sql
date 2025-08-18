CREATE TABLE tags
(
    tag_id       SERIAL PRIMARY KEY,
    subtag_id    INTEGER REFERENCES tag_subtags (subtag_id),
    namespace_id INTEGER REFERENCES tag_namespaces (namespace_id),
    UNIQUE (namespace_id, subtag_id)
);