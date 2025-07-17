CREATE TABLE aliased_siblings
(
    original_younger_id INTEGER REFERENCES tags (tag_id),
    original_older_id   INTEGER REFERENCES tags (tag_id),
    younger_id          INTEGER REFERENCES tags (tag_id),
    older_id            INTEGER REFERENCES tags (tag_id),
    domain_id           INTEGER REFERENCES tag_domains (tag_domain_id),
    UNIQUE (original_younger_id, original_older_id, domain_id)
);

CREATE OR REPLACE FUNCTION insert_aliased_siblings_insert() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN

    INSERT INTO aliased_siblings(original_younger_id, original_older_id, younger_id, older_id, domain_id)
    SELECT new.younger_id,
           new.older_id,
           COALESCE(younger_alias.alias_id, new.younger_id),
           COALESCE(older_alias.alias_id, new.older_id),
           new.domain_id
    FROM (SELECT new.older_id, new.younger_id) n
             LEFT JOIN flattened_aliases younger_alias ON younger_alias.alias_id = new.younger_id AND younger_alias.domain_id = new.domain_id
             LEFT JOIN flattened_aliases older_alias ON older_alias.alias_id = new.older_id AND older_alias.domain_id = new.domain_id;


    RETURN new;
END;
$$;

CREATE TRIGGER insert_aliased_siblings_insert
    AFTER INSERT
    ON tag_siblings
    FOR EACH ROW
EXECUTE FUNCTION insert_aliased_siblings_insert();

CREATE OR REPLACE FUNCTION update_aliased_siblings() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    CASE
        WHEN tg_op = 'UPDATE' THEN -- Updated.
        -- Example: `(aliased, alias, domain)` ('toujou koneko', 'character:toujou koneko', 1) -> ('toujou koneko', 'character:shrione', 1)
            UPDATE aliased_siblings SET younger_id = new.alias_id WHERE original_younger_id = old.aliased_id AND domain_id = new.domain_id;
            UPDATE aliased_siblings SET older_id = new.alias_id WHERE original_older_id = old.aliased_id AND domain_id = new.domain_id;
            RETURN new;
        WHEN tg_op = 'INSERT' THEN -- INSERT
        UPDATE aliased_siblings SET younger_id = new.alias_id WHERE original_younger_id = new.aliased_id AND domain_id = new.domain_id;
        UPDATE aliased_siblings SET older_id = new.alias_id WHERE original_older_id = new.aliased_id AND domain_id = new.domain_id;
        RETURN new;
        WHEN tg_op = 'DELETE' THEN -- DELETE
        -- Update any records to either their original ids, or if aliased_siblings still has a replacement for it, use that instead
            UPDATE aliased_siblings sib
            SET younger_id = COALESCE((SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.alias_id), sib.original_younger_id),
                older_id   = COALESCE((SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.alias_id), sib.original_older_id)
            WHERE sib.domain_id = old.domain_id
              AND (sib.older_id = old.alias_id OR sib.younger_id = old.alias_id);
            RETURN new;
        END CASE;
END;
$$;

CREATE TRIGGER update_aliased_siblings
    AFTER UPDATE OR INSERT OR DELETE
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION update_aliased_siblings();

