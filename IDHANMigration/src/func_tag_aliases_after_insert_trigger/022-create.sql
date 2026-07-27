CREATE OR REPLACE FUNCTION tag_aliases_after_insert_trigger()
    RETURNS TRIGGER
AS
$$
BEGIN
    -- Update any aliases where we are the new ideal
    UPDATE tag_aliases
    SET ideal_alias_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE COALESCE(tag_aliases.ideal_alias_id, tag_aliases.alias_id) = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    -- update any parents where we are the new ideal
    UPDATE tag_parents
    SET ideal_parent_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE parent_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    -- update any children where we are the new ideal
    UPDATE tag_parents
    SET ideal_child_id = COALESCE(new.ideal_alias_id, new.alias_id)
    WHERE child_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create trigger
CREATE TRIGGER tag_aliases_after_insert
    AFTER INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION tag_aliases_after_insert_trigger();
