-- Two-arg overload only. It must be created before the tags table (slot 009), whose tag_text
-- STORED generated column calls it. Its LANGUAGE sql body references only tag_namespaces (005)
-- and tag_subtags (006), so it is valid to create here. The one-arg tag_text(tag_id) overload
-- reads the tags table itself and so must come after it -- see func_tag_text_by_id/.
CREATE OR REPLACE FUNCTION tag_text(namespace_id_i INTEGER, subtag_id_i INTEGER) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT concat_tag(
               COALESCE((SELECT namespace_text FROM tag_namespaces WHERE namespace_id = namespace_id_i), ''),
               COALESCE((SELECT subtag_text FROM tag_subtags WHERE subtag_id = subtag_id_i), 'unknown')
       );
$$;
