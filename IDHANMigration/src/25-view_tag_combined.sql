CREATE TABLE tags_combined
(
    tag_id INTEGER REFERENCES tags (tag_id),
    tag_text TEXT,
    PRIMARY KEY (tag_id)
);

CREATE OR REPLACE FUNCTION concat_tag(namespace_text TEXT, subtag_text TEXT) RETURNS TEXT AS
$$
BEGIN
    RETURN CASE WHEN namespace_text = '' THEN subtag_text ELSE namespace_text || ':' || subtag_text END;
END;
$$ LANGUAGE plpgsql STABLE;

CREATE OR REPLACE FUNCTION update_tags_combined() RETURNS TRIGGER AS
$$
BEGIN
    IF (tg_op = 'INSERT') THEN
        INSERT INTO tags_combined (tag_id, tag_text)
        VALUES (new.tag_id,
                concat_tag((SELECT namespace_text FROM tag_namespaces WHERE namespace_id = new.namespace_id LIMIT 1),
                           (SELECT subtag_text FROM tag_subtags WHERE subtag_id = new.subtag_id LIMIT 1)));
        RETURN new;
    ELSIF (tg_op = 'DELETE') THEN
        DELETE FROM tags_combined WHERE tag_id = old.tag_id;
        RETURN old;
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tags_combined_insert
    AFTER INSERT
    ON tags
    FOR EACH ROW
EXECUTE FUNCTION update_tags_combined();

CREATE TRIGGER tags_combined_delete
    AFTER DELETE
    ON tags
    FOR EACH ROW
EXECUTE FUNCTION update_tags_combined();

INSERT INTO tags_combined (tag_id, tag_text)
SELECT tag_id, concat_tag(namespace_text, subtag_text)
FROM tags
         NATURAL JOIN tag_namespaces
         NATURAL JOIN tag_subtags;