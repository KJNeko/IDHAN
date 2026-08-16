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
    IF ARRAY_LENGTH(namespaces, 1) != ARRAY_LENGTH(subtags, 1) THEN
        RAISE EXCEPTION 'Arrays must have the same length. Namespaces: %, Subtags: %',
            ARRAY_LENGTH(namespaces, 1), ARRAY_LENGTH(subtags, 1);
    END IF;

    INSERT INTO tag_namespaces (namespace_text)
    SELECT DISTINCT n.namespace_text
    FROM UNNEST(namespaces) n(namespace_text)
    ORDER BY n.namespace_text
    ON CONFLICT (namespace_text) DO NOTHING;

    WITH folded AS (SELECT DISTINCT CASEFOLD(NORMALIZE(t.namespace_text, NFC))              AS namespace_text,
                                    NORMALIZE(CASEFOLD(NORMALIZE(t.subtag_text, NFC)), NFC) AS subtag_text
                    FROM UNNEST(namespaces, subtags) AS t(namespace_text, subtag_text))
    INSERT
    INTO tags (namespace_id, subtag_text)
    SELECT tag_namespaces.namespace_id, folded.subtag_text
    FROM folded
             JOIN tag_namespaces ON tag_namespaces.namespace_text = folded.namespace_text
    ORDER BY tag_namespaces.namespace_id, folded.subtag_text
    ON CONFLICT (namespace_id, subtag_text) DO NOTHING;

    RETURN QUERY
        WITH folded AS (SELECT t.ord,
                               CASEFOLD(NORMALIZE(t.namespace_text, NFC))              AS namespace_text,
                               NORMALIZE(CASEFOLD(NORMALIZE(t.subtag_text, NFC)), NFC) AS subtag_text
                        FROM UNNEST(namespaces, subtags) WITH ORDINALITY AS t(namespace_text, subtag_text, ord))
        SELECT tags.tag_id, folded.namespace_text, folded.subtag_text
        FROM folded
                 LEFT JOIN tag_namespaces ON tag_namespaces.namespace_text = folded.namespace_text
                 LEFT JOIN tags ON tags.namespace_id = tag_namespaces.namespace_id
            AND tags.subtag_text = folded.subtag_text
        ORDER BY folded.ord;
END;
$$ LANGUAGE plpgsql;
