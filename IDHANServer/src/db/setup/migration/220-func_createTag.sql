CREATE OR REPLACE FUNCTION createtag(namespace_text_i TEXT, subtag_text_i TEXT) RETURNS INTEGER AS
$$

DECLARE
    tag_id_v       INTEGER;
    namespace_id_v INTEGER;
    subtag_id_v    INTEGER;
BEGIN

    SELECT namespace_id FROM tag_namespaces WHERE namespace_text = namespace_text_i INTO namespace_id_v;

    IF namespace_id_v IS NULL THEN
        INSERT INTO tag_namespaces (namespace_text)
        VALUES (namespace_text_i)
        RETURNING namespace_id INTO namespace_id_v;
    END IF;

    SELECT subtag_id FROM tag_subtags WHERE subtag_text = subtag_text_i INTO subtag_id_v;

    IF subtag_id_v IS NULL THEN
        INSERT INTO tag_subtags (subtag_text) VALUES (subtag_text_i) RETURNING subtag_id INTO subtag_id_v;
    END IF;

    SELECT tag_id FROM tags WHERE namespace_id = namespace_id_v AND subtag_id = subtag_id_v INTO tag_id_v;

    IF tag_id_v IS NULL THEN
        INSERT INTO tags (namespace_id, subtag_id) VALUES (namespace_id_v, subtag_id_v) RETURNING tag_id INTO tag_id_v;
    END IF;

    RETURN tag_id_v;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION createtag(tag_string text) RETURNS integer
    LANGUAGE plpgsql
AS
$$
DECLARE
    namespace_text_v text;
    subtag_text_v    text;
    split_pos        integer;
BEGIN
    -- Find the position of the ':' character
    split_pos := POSITION(':' IN tag_string);

    -- If no ':' is found, return null
    IF split_pos = 0 THEN
        RETURN createtag('', tag_string);
    END IF;

    -- Split the string into namespace and subtag
    namespace_text_v := SUBSTRING(tag_string FROM 1 FOR split_pos - 1);
    subtag_text_v := SUBSTRING(tag_string FROM split_pos + 1);

    -- Use the existing createtag function
    RETURN createtag(namespace_text_v, subtag_text_v);
END;
$$;

ALTER FUNCTION createtag(text) OWNER TO idhan;
