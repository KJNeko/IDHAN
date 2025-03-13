CREATE OR REPLACE FUNCTION createBatchedTag(namespace_text_i TEXT[], subtag_text_i TEXT[])
    RETURNS SETOF INTEGER
AS
$$
DECLARE
    namespace_ids INTEGER[]; subtag_ids INTEGER[]; tag_ids INTEGER[];
BEGIN

    -- get all namespace ids
    SELECT array_agg((CASE
                          WHEN tag_namespaces.namespace_id IS NULL THEN 0::INTEGER
                          ELSE tag_namespaces.namespace_id END))
    INTO namespace_ids
    FROM UNNEST(namespace_text_i) as i(namespace_text)
             LEFT JOIN tag_namespaces ON i.namespace_text = tag_namespaces.namespace_text;

    -- get all subtag ids
    SELECT array_agg((CASE WHEN ts.subtag_id IS NULL THEN 0::INTEGER ELSE ts.subtag_id END))
    INTO subtag_ids
    FROM UNNEST(subtag_text_i) as i(subtag_text)
             LEFT JOIN tag_subtags as ts ON i.subtag_text = ts.subtag_text;

    IF (SELECT count(*) FROM UNNEST(namespace_ids) AS ns(namespace_id) WHERE ns.namespace_id = 0) > 0 THEN
        LOCK TABLE tag_namespaces IN SHARE UPDATE EXCLUSIVE MODE;

        INSERT INTO tag_namespaces(namespace_text)
        SELECT DISTINCT*
        FROM UNNEST(namespace_text_i) as i(namespace_text)
        ON CONFLICT(namespace_text) DO NOTHING;
    END IF;

    IF (SELECT count(*) FROM UNNEST(subtag_ids) AS ss(subtag_id) WHERE ss.subtag_id = 0) > 0 THEN
        LOCK TABLE tag_subtags IN SHARE UPDATE EXCLUSIVE MODE;

        INSERT INTO tag_subtags(subtag_text)
        SELECT DISTINCT *
        FROM UNNEST(subtag_text_i) as i(subtag_text)
        ON CONFLICT(subtag_text) DO NOTHING;
    END IF;

    SELECT array_agg((CASE WHEN tags.tag_id IS NULL THEN 0::INTEGER ELSE tags.tag_id END))
    INTO tag_ids
    FROM UNNEST(namespace_ids, subtag_ids) as ids(namespace_id, subtag_id)
             LEFT JOIN tags ON ids.namespace_id = tags.namespace_id AND ids.subtag_id = tags.subtag_id;

    IF (SELECT COUNT(*) FROM UNNEST(tag_ids) AS tags(tag_id) WHERE tags.tag_id = 0) > 0 THEN
        LOCK TABLE tags IN SHARE UPDATE EXCLUSIVE MODE;

        INSERT
        INTO tags (namespace_id, subtag_id)
        SELECT existing_ns.namespace_id,
               existing_ss.subtag_id
        FROM UNNEST(namespace_text_i, subtag_text_i) as i(namespace_text, subtag_text)
                 LEFT JOIN tag_namespaces existing_ns ON i.namespace_text = existing_ns.namespace_text
                 LEFT JOIN tag_subtags existing_ss ON i.subtag_text = existing_ss.subtag_text
        ON CONFLICT DO NOTHING;
    END IF;

    RETURN QUERY SELECT tag_id
                 FROM UNNEST(namespace_text_i, subtag_text_i) WITH ORDINALITY as i(namespace_text, subtag_text, idx)
                          LEFT JOIN tag_namespaces as tn ON tn.namespace_text = i.namespace_text
                          LEFT JOIN tag_subtags as ts ON ts.subtag_text = i.subtag_text
                          LEFT JOIN tags ON tn.namespace_id = tags.namespace_id AND ts.subtag_id = tags.subtag_id
                 ORDER BY i.idx;
END;
$$ LANGUAGE plpgsql;

