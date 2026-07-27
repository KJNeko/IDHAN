-- LANGUAGE sql IMMUTABLE so the planner can inline it. tags.tag_text is a STORED generated column
-- (via tag_text()), so this runs once per inserted tag row; the sql form avoids the SPI/exception
-- overhead a plpgsql body would pay. IMMUTABLE is correct: namespace/subtag text is never modified.
CREATE OR REPLACE FUNCTION concat_tag(namespace_text TEXT, subtag_text TEXT) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END;
$$;
