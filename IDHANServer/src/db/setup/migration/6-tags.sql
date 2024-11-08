CREATE TABLE tags
(
	tag_id       BIGSERIAL PRIMARY KEY,
	namespace_id INTEGER REFERENCES tag_namespaces (namespace_id),
	subtag_id    INTEGER REFERENCES tag_subtags (subtag_id),
	UNIQUE (namespace_id, subtag_id)
);