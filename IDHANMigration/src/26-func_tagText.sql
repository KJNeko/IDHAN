CREATE OR REPLACE FUNCTION tag_text(p_tag_id BIGINT)
    RETURNS TEXT
    LANGUAGE plpgsql
    STABLE
AS
$$
DECLARE
    text TEXT;
BEGIN

    text := 'unknown';

    SELECT tag_text
    INTO text
    FROM tags_combined
    WHERE tag_id = p_tag_id
    LIMIT 1;

    RETURN text;
END;
$$;