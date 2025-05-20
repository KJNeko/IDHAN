CREATE INDEX ON tag_subtags USING HASH (subtag_text);
CREATE INDEX ON tag_namespaces USING HASH (namespace_text);