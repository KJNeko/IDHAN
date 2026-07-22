-- Rewrite the tag_text helpers as LANGUAGE sql IMMUTABLE functions so the planner can inline
-- them. The previous plpgsql versions paid full SPI/exception-context overhead on every call,
-- and tags.tag_text is a STORED generated column, so tag_text() runs once per inserted tag row.
-- IMMUTABLE is intentional and correct: namespace/subtag text is never modified once created.
CREATE OR REPLACE FUNCTION concat_tag(namespace_text TEXT, subtag_text TEXT) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END;
$$;

-- COALESCE preserves the defensive defaults of the original plpgsql body ('' namespace,
-- 'unknown' subtag) for the theoretical case where a component id has no matching row.
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

CREATE OR REPLACE FUNCTION tag_text(tag_id_i INTEGER) RETURNS TEXT
    LANGUAGE sql
    IMMUTABLE
AS
$$
SELECT tag_text(namespace_id, subtag_id)
FROM tags
WHERE tag_id = tag_id_i;
$$;
