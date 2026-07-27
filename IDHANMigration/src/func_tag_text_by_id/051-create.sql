-- One-arg tag_text(tag_id) overload. Its LANGUAGE sql body reads the tags table, and sql function
-- bodies are validated against referenced relations at creation time -- so this must be created
-- AFTER table_tags (009). It is placed here (just after the tags index at 050) rather than beside
-- the two-arg overload at 008, which has to precede tags for the generated column. Nothing during
-- migration calls this overload; it is a query-time convenience helper.
CREATE OR REPLACE FUNCTION tag_text(tag_id_i INTEGER) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT tag_text(namespace_id, subtag_id)
FROM tags
WHERE tag_id = tag_id_i;
$$;
