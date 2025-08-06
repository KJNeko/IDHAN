CREATE OR REPLACE FUNCTION createbatchtags(p_namespaces TEXT[], p_subtags TEXT[])
    RETURNS TABLE
            (
                tag_id         INTEGER,
                namespace_text TEXT,
                subtag_text    TEXT
            )
AS
$$
BEGIN
    INSERT INTO tag_subtags (subtag_text)
    SELECT DISTINCT *
    FROM UNNEST(p_subtags)
    ON CONFLICT DO NOTHING;

    INSERT INTO tag_namespaces (namespace_text)
    SELECT DISTINCT *
    FROM UNNEST(p_namespaces)
    ON CONFLICT DO NOTHING;

    INSERT INTO tags (namespace_id, subtag_id)
    SELECT DISTINCT namespace_id, subtag_id
    FROM UNNEST(p_namespaces, p_subtags) AS t(namespace_text, subtag_text)
             JOIN tag_namespaces USING (namespace_text)
             JOIN tag_subtags USING (subtag_text)
    ON CONFLICT DO NOTHING;

    RETURN QUERY
        SELECT tags.tag_id AS tag_id, tag_namespaces.namespace_text AS namespace_text, tag_subtags.subtag_text AS subtag_text
        FROM UNNEST(p_namespaces, p_subtags) AS t(namespace_text, subtag_text)
                 JOIN tag_namespaces ON tag_namespaces.namespace_text = t.namespace_text
                 JOIN tag_subtags ON tag_subtags.subtag_text = t.subtag_text
                 JOIN tags
                      ON tags.namespace_id = tag_namespaces.namespace_id AND tags.subtag_id = tag_subtags.subtag_id;
END;
$$ LANGUAGE plpgsql VOLATILE;
