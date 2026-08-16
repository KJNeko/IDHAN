ALTER TABLE tags
    ADD COLUMN subtag_text TEXT NOT NULL;

UPDATE tags
SET subtag_text = (SELECT tag_subtags.subtag_text FROM tag_subtags WHERE tag_subtags.subtag_id = tags.subtag_id);