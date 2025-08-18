-- Create trigger function
CREATE OR REPLACE FUNCTION tag_aliases_after_delete_trigger()
    RETURNS TRIGGER
    LANGUAGE plpgsql
AS
$$
BEGIN

    UPDATE tag_aliases
    SET ideal_alias_id = NULL
    WHERE alias_id = old.aliased_id
      AND tag_domain_id = old.tag_domain_id;

    UPDATE tag_parents
    SET ideal_parent_id = NULL
    WHERE parent_id = old.aliased_id
      AND tag_domain_id = old.tag_domain_id;

    UPDATE tag_parents
    SET ideal_child_id = NULL
    WHERE child_id = old.aliased_id
      AND tag_domain_id = old.tag_domain_id;

    RETURN NULL;
END;
$$;

-- Create trigger
CREATE TRIGGER tag_aliases_after_delete
    AFTER DELETE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION tag_aliases_after_delete_trigger();