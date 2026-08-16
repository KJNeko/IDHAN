ALTER TABLE tags
    ADD UNIQUE (namespace_id, subtag_text);
