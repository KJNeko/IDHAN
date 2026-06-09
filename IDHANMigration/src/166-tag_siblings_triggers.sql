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
    new.ideal_older_id = (SELECT COALESCE(ts3.ideal_older_id, ts3.older_id)
                          FROM tag_siblings ts3
                          WHERE ts3.younger_id = new.older_id
                            AND ts3.tag_domain_id = new.tag_domain_id);

    new.ideal_younger_id = (SELECT COALESCE(ts4.ideal_younger_id, ts4.younger_id)
                            FROM tag_siblings ts4
                            WHERE ts4.older_id = new.younger_id
                              AND ts4.tag_domain_id = new.tag_domain_id);

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
    UPDATE tag_siblings
    SET ideal_younger_id = COALESCE(new.ideal_younger_id, new.younger_id)
    WHERE younger_id = new.older_id
      AND tag_domain_id = new.tag_domain_id;

    UPDATE tag_siblings
    SET ideal_older_id = COALESCE(new.ideal_older_id, new.older_id)
    WHERE older_id = new.younger_id
      AND tag_domain_id = new.tag_domain_id;

    UPDATE tag_siblings
    SET ideal_younger_id = COALESCE(new.ideal_younger_id, new.younger_id)
    WHERE ideal_younger_id = new.older_id
      AND tag_domain_id = new.tag_domain_id;

    UPDATE tag_siblings
    SET ideal_older_id = COALESCE(new.ideal_older_id, new.older_id)
    WHERE ideal_older_id = new.younger_id
      AND tag_domain_id = new.tag_domain_id;

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
    UPDATE tag_siblings
    SET ideal_younger_id = (SELECT COALESCE(ts2.ideal_younger_id, ts2.younger_id)
                            FROM tag_siblings ts2
                            WHERE ts2.older_id = tag_siblings.younger_id
                              AND ts2.tag_domain_id = tag_siblings.tag_domain_id)
    WHERE tag_domain_id = old.tag_domain_id
      AND (younger_id = old.older_id OR ideal_younger_id = old.younger_id);

    UPDATE tag_siblings
    SET ideal_older_id = (SELECT COALESCE(ts2.ideal_older_id, ts2.older_id)
                          FROM tag_siblings ts2
                          WHERE ts2.younger_id = tag_siblings.older_id
                            AND ts2.tag_domain_id = tag_siblings.tag_domain_id)
    WHERE tag_domain_id = old.tag_domain_id
      AND (older_id = old.younger_id OR ideal_older_id = old.older_id);

    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_siblings_after_delete
    AFTER DELETE
    ON tag_siblings
    FOR EACH ROW
EXECUTE FUNCTION tag_siblings_after_delete();
