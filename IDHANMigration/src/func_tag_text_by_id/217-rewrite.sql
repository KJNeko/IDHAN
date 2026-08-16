CREATE OR REPLACE FUNCTION tag_text(tag_id_i INTEGER) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT tag_text(namespace_id, subtag_text)
FROM tags
WHERE tag_id = tag_id_i;
$$;
