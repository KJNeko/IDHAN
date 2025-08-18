CREATE SEQUENCE tags_tag_id_seq;

CREATE TABLE tags
(
    tag_id       tagid PRIMARY KEY DEFAULT NEXTVAL('tags_tag_id_seq'),
    subtag_id    subtagid REFERENCES tag_subtags (subtag_id),
    namespace_id namespaceid REFERENCES tag_namespaces (namespace_id),
    UNIQUE (namespace_id, subtag_id)
);