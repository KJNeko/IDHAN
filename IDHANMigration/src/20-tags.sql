CREATE OR REPLACE FUNCTION concat_tag(namespace_text TEXT, subtag_text TEXT) RETURNS TEXT AS
$$
BEGIN
    RETURN CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION tag_text(namespace_id_i INTEGER, subtag_id_i INTEGER)
    RETURNS TEXT
AS
$$
DECLARE
    namespace_text_v TEXT;
    subtag_text_v    TEXT;
BEGIN

    namespace_text_v := '';
    subtag_text_v := 'unknown';

    SELECT namespace_text INTO namespace_text_v FROM tag_namespaces WHERE namespace_id = namespace_id_i;
    SELECT subtag_text INTO subtag_text_v FROM tag_subtags WHERE subtag_id = subtag_id_i;

    RETURN concat_tag(namespace_text_v, subtag_text_v);
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION tag_text(tag_id_i INTEGER)
    RETURNS TEXT
AS
$$
DECLARE
    namespace_id_v INTEGER;
    subtag_id_v    INTEGER;
BEGIN

    SELECT namespace_id, subtag_id INTO namespace_id_v, subtag_id_v FROM tags WHERE tag_id = tag_id_i;

    RETURN tag_text(namespace_id_v, subtag_id_v);
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE TABLE tags
(
    tag_id       SERIAL PRIMARY KEY,
    subtag_id    INTEGER REFERENCES tag_subtags (subtag_id),
    namespace_id INTEGER REFERENCES tag_namespaces (namespace_id),
    tag_text     TEXT GENERATED ALWAYS AS ( tag_text(namespace_id, subtag_id) ) STORED,
    UNIQUE (namespace_id, subtag_id)
);