CREATE INDEX ON tag_subtags USING hash (subtag_text);
CREATE INDEX ON tag_namespaces USING hash (namespace_text);