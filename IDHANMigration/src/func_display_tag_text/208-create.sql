CREATE OR REPLACE FUNCTION display_tag_text(namespace_id_i INTEGER, subtag_id_i INTEGER) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT concat_tag(
               COALESCE((SELECT namespace_text FROM tag_namespaces WHERE namespace_id = namespace_id_i), ''),
               COALESCE((SELECT COALESCE(display_text, subtag_text) FROM tag_subtags WHERE subtag_id = subtag_id_i),
                        'unknown')
       );
$$;
