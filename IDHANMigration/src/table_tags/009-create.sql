CREATE TABLE tags
(
    tag_id       SERIAL PRIMARY KEY,
    subtag_id    INTEGER REFERENCES tag_subtags (subtag_id),
    namespace_id INTEGER REFERENCES tag_namespaces (namespace_id),
    tag_text     TEXT GENERATED ALWAYS AS ( tag_text(namespace_id, subtag_id) ) STORED,
    UNIQUE (namespace_id, subtag_id)
);
