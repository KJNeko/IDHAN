ALTER TABLE tag_siblings
    ADD COLUMN ideal_older_id   INTEGER REFERENCES tags (tag_id) NULL,
    ADD COLUMN ideal_younger_id INTEGER REFERENCES tags (tag_id) NULL;

ALTER TABLE tag_siblings
    ADD COLUMN effective_older_id INTEGER GENERATED ALWAYS AS (COALESCE(ideal_older_id, older_id)) VIRTUAL;

ALTER TABLE tag_siblings
    ADD COLUMN effective_younger_id INTEGER GENERATED ALWAYS AS (COALESCE(ideal_younger_id, younger_id)) VIRTUAL;

ALTER TABLE tag_siblings
    ADD UNIQUE (tag_domain_id, younger_id);

CREATE OR REPLACE FUNCTION tag_siblings_before_insert()
    RETURNS TRIGGER
AS
$$
BEGIN
    new.ideal_older_id = (SELECT ta.effective_tag_id
                          FROM tag_aliases ta
                          WHERE ta.aliased_id = new.older_id
                            AND ta.tag_domain_id = new.tag_domain_id
                          LIMIT 1);

    new.ideal_younger_id = (SELECT ta.effective_tag_id
                            FROM tag_aliases ta
                            WHERE ta.aliased_id = new.younger_id
                              AND ta.tag_domain_id = new.tag_domain_id
                            LIMIT 1);

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_siblings_before_insert
    BEFORE INSERT
    ON tag_siblings
    FOR EACH ROW
EXECUTE FUNCTION tag_siblings_before_insert();

CREATE OR REPLACE FUNCTION tag_siblings_after_insert()
    RETURNS TRIGGER
AS
$$
BEGIN
    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_siblings_after_insert
    AFTER INSERT
    ON tag_siblings
    FOR EACH ROW
EXECUTE FUNCTION tag_siblings_after_insert();

CREATE OR REPLACE FUNCTION tag_siblings_after_delete()
    RETURNS TRIGGER
AS
$$
BEGIN
    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_siblings_after_delete
    AFTER DELETE
    ON tag_siblings
    FOR EACH ROW
EXECUTE FUNCTION tag_siblings_after_delete();
