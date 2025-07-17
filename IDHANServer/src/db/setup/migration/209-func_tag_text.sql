CREATE OR REPLACE FUNCTION tag_text(tag_id_i INTEGER) RETURNS TEXT
    LANGUAGE plpgsql AS
$$
DECLARE
    v_tag_text TEXT;
BEGIN
    SELECT tag_text
    INTO v_tag_text
    FROM tags_combined tc
    WHERE tc.tag_id = tag_id_i;

    RETURN v_tag_text;
END;
$$ IMMUTABLE;