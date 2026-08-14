CREATE OR REPLACE FUNCTION concat_tag(namespace_text TEXT, subtag_text TEXT) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END;
$$;
