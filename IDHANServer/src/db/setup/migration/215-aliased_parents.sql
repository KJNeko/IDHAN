CREATE TABLE aliased_parents
(
    original_parent_id INTEGER REFERENCES tags (tag_id)                NOT NULL,
    original_child_id  INTEGER REFERENCES tags (tag_id)                NOT NULL,
    parent_id          INTEGER REFERENCES tags (tag_id)                NOT NULL,
    child_id           INTEGER REFERENCES tags (tag_id)                NOT NULL,
    domain_id          SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    UNIQUE (original_parent_id, original_child_id, domain_id)
);

CREATE OR REPLACE FUNCTION update_aliased_parents() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    IF old.aliased_id IS NOT NULL THEN
        RAISE DEBUG 'Updating any parents that point to % -> %', tag_text(old.alias_id), tag_text(new.alias_id);
        UPDATE aliased_parents SET parent_id = new.alias_id WHERE parent_id = old.alias_id AND domain_id = old.domain_id;
        RAISE DEBUG 'Updating any children that point to % -> %', tag_text(old.alias_id), tag_text(new.alias_id);
        UPDATE aliased_parents SET child_id = new.alias_id WHERE child_id = old.alias_id AND domain_id = old.domain_id;
    END IF;

    RAISE DEBUG 'Forwarding any parents that point to % -> %', tag_text(new.aliased_id), tag_text(new.alias_id);
    UPDATE aliased_parents SET parent_id = new.alias_id WHERE parent_id = new.aliased_id AND domain_id = new.domain_id;
    RAISE DEBUG 'Forwarding any children that point to % -> %', tag_text(new.aliased_id), tag_text(new.alias_id);
    UPDATE aliased_parents SET child_id = new.alias_id WHERE child_id = new.aliased_id AND domain_id = new.domain_id;

    RETURN new;
END;
$$;

CREATE OR REPLACE FUNCTION update_aliased_parents_insert() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    RAISE DEBUG 'Inserting aliased parent % of child %', tag_text(new.parent_id), tag_text(new.child_id);

    INSERT INTO aliased_parents (original_parent_id, original_child_id, parent_id, child_id, domain_id)
    SELECT new.parent_id,
           new.child_id,
           COALESCE(fa_parent.alias_id, new.parent_id),
           COALESCE(fa_child.alias_id, new.child_id),
           new.domain_id
    FROM (SELECT new.parent_id, new.child_id, new.domain_id) n
             LEFT JOIN flattened_aliases fa_parent ON fa_parent.aliased_id = new.parent_id AND fa_parent.domain_id = new.domain_id
             LEFT JOIN flattened_aliases fa_child ON fa_child.aliased_id = new.child_id AND fa_child.domain_id = new.domain_id;

    RETURN new;
END;
$$;

CREATE OR REPLACE FUNCTION remove_aliased_parents() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN

    RAISE DEBUG 'Updating any parent that points to % to its default state', tag_text(old.aliased_id);
    RAISE DEBUG 'Updating any child that points to % to its default state', tag_text(old.aliased_id);

    UPDATE aliased_parents ap
    SET parent_id = COALESCE((SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.alias_id), ap.original_parent_id),
        child_id  = COALESCE((SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.alias_id), ap.original_child_id)
    WHERE ap.domain_id = old.domain_id
      AND (ap.parent_id = old.alias_id OR ap.child_id = old.alias_id);

    RETURN old;
END;
$$;

CREATE OR REPLACE TRIGGER insert_tag_parents_aliased_parents
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION update_aliased_parents_insert();

CREATE OR REPLACE TRIGGER update_flattened_aliases_aliased_parents
    AFTER UPDATE OR INSERT
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION update_aliased_parents();

CREATE OR REPLACE TRIGGER delete_flattened_aliases_aliased_parents
    AFTER DELETE
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION remove_aliased_parents();

CREATE VIEW unique_aliased_parents AS
(
SELECT DISTINCT parent_id, child_id, domain_id
FROM aliased_parents );