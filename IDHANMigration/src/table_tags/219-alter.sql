ALTER TABLE tags
    ALTER COLUMN tag_text SET EXPRESSION AS (tag_text(namespace_id, subtag_text));

ALTER TABLE tags
    DROP COLUMN subtag_id;
