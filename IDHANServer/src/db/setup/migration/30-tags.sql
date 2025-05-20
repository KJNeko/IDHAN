CREATE TABLE tags
(
    tag_id       SERIAL PRIMARY KEY,
    namespace_id INTEGER REFERENCES tag_namespaces (namespace_id) NOT NULL,
    subtag_id    INTEGER REFERENCES tag_subtags (subtag_id)       NOT NULL,
    UNIQUE (namespace_id, subtag_id)
);