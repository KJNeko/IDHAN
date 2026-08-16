CREATE OR REPLACE FUNCTION tag_text(namespace_id_i INTEGER, subtag_text_i TEXT) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT concat_tag(
               COALESCE((SELECT namespace_text FROM tag_namespaces WHERE namespace_id = namespace_id_i), ''),
               subtag_text_i
       );
$$;
