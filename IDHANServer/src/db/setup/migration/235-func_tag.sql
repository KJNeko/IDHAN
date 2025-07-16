CREATE OR REPLACE FUNCTION tag(in_namespace_text TEXT, in_subtag_text TEXT)
    RETURNS BIGINT AS
$tag_id$
DECLARE
    in_namespace_id SMALLINT := 0;
    in_subtag_id    INTEGER  := 0;
    return_value    BIGINT   := 0;
BEGIN
    IF (SELECT COUNT(*) FROM tag_namespaces WHERE namespace_text = in_namespace_text) = 1::bigint THEN
        --The ID exists. Set it
        SELECT namespace_id INTO in_namespace_id FROM tag_namespaces WHERE namespace_text = in_namespace_text LIMIT 1;
    ELSE
        --No ID. Create one
        INSERT INTO tag_namespaces (namespace_text)
        VALUES (in_namespace_text)
        RETURNING namespace_id INTO in_namespace_id;
    END IF;

    IF (SELECT COUNT(*) FROM tag_subtags WHERE subtag_text = in_subtag_text) = 1::bigint THEN
        --The ID exists. Set it
        SELECT subtag_id INTO in_subtag_id FROM tag_subtags WHERE subtag_text = in_subtag_text LIMIT 1;
    ELSE
        --No ID. Create one
        INSERT INTO tag_subtags (subtag_text) VALUES (in_subtag_text) RETURNING subtag_id INTO in_subtag_id;
    END IF;

    INSERT INTO tags (namespace_id, subtag_id)
    VALUES (in_namespace_id, in_subtag_id)
    RETURNING tag_id INTO return_value;
    RETURN return_value;
END;
$tag_id$ LANGUAGE plpgsql;