
CREATE OR REPLACE FUNCTION createbatchtags(
    namespaces TEXT[],
    subtags TEXT[]
)
    RETURNS TABLE
            (
                tag_id         INTEGER,
                namespace_text TEXT,
                subtag_text    TEXT
            )
AS
$$
    # variable_conflict use_column
BEGIN
    -- Check if arrays have the same length
    IF ARRAY_LENGTH(namespaces, 1) != ARRAY_LENGTH(subtags, 1) THEN
        RAISE EXCEPTION 'Arrays must have the same length. Namespaces: %, Subtags: %',
            ARRAY_LENGTH(namespaces, 1), ARRAY_LENGTH(subtags, 1);
    END IF;

    -- namespaces
    INSERT INTO tag_namespaces (namespace_text)
    SELECT DISTINCT n.namespace_text
    FROM UNNEST(namespaces) n(namespace_text)
    ON CONFLICT (namespace_text) DO NOTHING;

    -- subtags
    INSERT INTO tag_subtags (subtag_text)
    SELECT DISTINCT s.subtag_text
    FROM UNNEST(subtags) s(subtag_text)
    ON CONFLICT (subtag_text) DO NOTHING;

    -- tags
    WITH tag_texts AS (SELECT DISTINCT t.namespace_text, t.subtag_text
                       FROM UNNEST(namespaces, subtags) AS t(namespace_text, subtag_text)),
         component_ids AS (SELECT DISTINCT namespace_id, subtag_id
                           FROM tag_texts
                                    JOIN tag_namespaces ON tag_namespaces.namespace_text = tag_texts.namespace_text
                                    JOIN tag_subtags ON tag_subtags.subtag_text = tag_texts.subtag_text)
    INSERT
    INTO tags (namespace_id, subtag_id)
    SELECT namespace_id, subtag_id
    FROM component_ids ci
    ON CONFLICT (namespace_id, subtag_id) DO NOTHING;

    RETURN QUERY
        WITH tag_texts AS (SELECT t.namespace_text, t.subtag_text, t.ord
                           FROM UNNEST(namespaces, subtags) WITH ORDINALITY AS t(namespace_text, subtag_text, ord)),
             component_ids AS (SELECT tag_texts.ord,
                                      tag_texts.namespace_text,
                                      tag_namespaces.namespace_id,
                                      tag_texts.subtag_text,
                                      tag_subtags.subtag_id
                               FROM tag_texts
                                        JOIN tag_namespaces ON tag_texts.namespace_text = tag_namespaces.namespace_text
                                        JOIN tag_subtags ON tag_texts.subtag_text = tag_subtags.subtag_text)
        SELECT tags.tag_id, ci.namespace_text, ci.subtag_text
        FROM component_ids ci
                 LEFT JOIN tags ON tags.namespace_id = ci.namespace_id AND tags.subtag_id = ci.subtag_id
        ORDER BY ci.ord;
END;
$$ LANGUAGE plpgsql;
