CREATE SEQUENCE tag_namespaces_namespace_id_seq;

CREATE TABLE tag_namespaces
(
    namespace_id   namespaceid PRIMARY KEY DEFAULT NEXTVAL('tag_namespaces_namespace_id_seq'),
    namespace_text TEXT NOT NULL UNIQUE
);