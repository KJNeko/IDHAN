CREATE TABLE aliased_parents
(
    original_parent_id INTEGER REFERENCES tags (tag_id)                NOT NULL,
    original_child_id  INTEGER REFERENCES tags (tag_id)                NOT NULL,
    parent_id INTEGER REFERENCES tags (tag_id) NULL,
    child_id  INTEGER REFERENCES tags (tag_id) NULL,
    domain_id          SMALLINT REFERENCES tag_domains (tag_domain_id) NOT NULL,
    UNIQUE (original_parent_id, original_child_id, domain_id)
);

CREATE FUNCTION a_ui_flattened_aliases() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    IF old.aliased_id IS NOT NULL THEN
        RAISE DEBUG 'Updating any parents that point to % -> %', tag_text(old.alias_id), tag_text(new.alias_id);
        UPDATE aliased_parents SET parent_id = new.alias_id WHERE parent_id = old.alias_id AND domain_id = old.domain_id AND parent_id != new.alias_id;
        RAISE DEBUG 'Updating any children that point to % -> %', tag_text(old.alias_id), tag_text(new.alias_id);
        UPDATE aliased_parents SET child_id = new.alias_id WHERE child_id = old.alias_id AND domain_id = old.domain_id AND child_id != new.alias_id;
    END IF;

    RAISE DEBUG 'Forwarding any parents that point to % -> %', tag_text(new.aliased_id), tag_text(new.alias_id);
    UPDATE aliased_parents SET parent_id = new.alias_id WHERE parent_id = new.aliased_id AND domain_id = new.domain_id AND parent_id != new.alias_id;
    RAISE DEBUG 'Forwarding any children that point to % -> %', tag_text(new.aliased_id), tag_text(new.alias_id);
    UPDATE aliased_parents SET child_id = new.alias_id WHERE child_id = new.aliased_id AND domain_id = new.domain_id AND child_id != new.alias_id;

    RETURN new;
END;
$$;

CREATE FUNCTION a_i_tag_parents() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    RAISE DEBUG 'Inserting aliased parent % of child %', tag_text(new.parent_id), tag_text(new.child_id);

    INSERT INTO aliased_parents (original_parent_id, original_child_id, parent_id, child_id, domain_id)
    SELECT new.parent_id,
           new.child_id,
           fa_parent.alias_id,
           fa_child.alias_id,
           new.domain_id
    FROM (SELECT new.parent_id, new.child_id, new.domain_id) n
             LEFT JOIN flattened_aliases fa_parent ON fa_parent.aliased_id = new.parent_id AND fa_parent.domain_id = new.domain_id
             LEFT JOIN flattened_aliases fa_child ON fa_child.aliased_id = new.child_id AND fa_child.domain_id = new.domain_id;

    RETURN new;
END;
$$;


CREATE FUNCTION a_d_tag_parents() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    RAISE DEBUG 'Deleting tag_parent % of child %', tag_text(old.parent_id), tag_text(old.child_id);
    DELETE FROM aliased_parents WHERE original_parent_id = old.parent_id AND original_child_id = old.child_id AND domain_id = old.domain_id;
    RETURN new;
END;
$$;

CREATE FUNCTION a_d_flattened_aliases() RETURNS TRIGGER
    LANGUAGE plpgsql AS
$$
BEGIN
    RAISE DEBUG 'Updating any parent that points to % to its default state', tag_text(old.aliased_id);
    RAISE DEBUG 'Updating any child that points to % to its default state', tag_text(old.aliased_id);

    UPDATE aliased_parents ap SET parent_id = (SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.aliased_id) WHERE ap.domain_id = old.domain_id AND ap.parent_id = old.aliased_id;
    UPDATE aliased_parents ap SET child_id = (SELECT alias_id FROM flattened_aliases fa WHERE fa.domain_id = old.domain_id AND fa.aliased_id = old.aliased_id) WHERE ap.domain_id = old.domain_id AND ap.child_id = old.aliased_id;

    RETURN new;
END;
$$;

CREATE TRIGGER after_insert_on_tag_parents
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION a_i_tag_parents();

CREATE TRIGGER after_delete_on_tag_parents
    AFTER DELETE
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION a_d_tag_parents();

CREATE TRIGGER after_update_o_insert_flattened_aliases
    AFTER UPDATE OR INSERT
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION a_ui_flattened_aliases();

CREATE TRIGGER after_delete_flattened_aliases
    AFTER DELETE
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION a_d_flattened_aliases();