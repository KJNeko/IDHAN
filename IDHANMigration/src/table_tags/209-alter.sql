ALTER TABLE tags
    ADD COLUMN display_tag_text TEXT GENERATED ALWAYS AS ( display_tag_text(namespace_id, subtag_id) ) STORED;
