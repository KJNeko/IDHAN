-- Create trigger function
CREATE OR REPLACE FUNCTION tag_aliases_after_update_trigger()
    RETURNS TRIGGER
    LANGUAGE plpgsql
AS
$$
BEGIN

    IF EXISTS(SELECT 1 FROM tag_aliases ta WHERE COALESCE(ta.ideal_alias_id, ta.alias_id) = new.aliased_id AND ta.tag_domain_id = new.tag_domain_id AND ta.aliased_id <> COALESCE(new.ideal_alias_id, new.alias_id))
    THEN
        RAISE EXCEPTION 'Recursive alias detected during update';
    END IF;

    UPDATE tag_aliases
    SET ideal_alias_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE tag_aliases.alias_id = new.aliased_id
      AND tag_aliases.tag_domain_id = new.tag_domain_id;

    UPDATE tag_parents
    SET ideal_parent_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE parent_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    UPDATE tag_parents
    SET ideal_child_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE child_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    RETURN new;
END;
$$;

-- Create trigger
CREATE TRIGGER tag_aliases_after_update
    AFTER UPDATE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION tag_aliases_after_update_trigger();