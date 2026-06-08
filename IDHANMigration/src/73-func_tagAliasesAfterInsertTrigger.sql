CREATE OR REPLACE FUNCTION tag_aliases_after_insert_trigger()
    RETURNS TRIGGER
AS
$$
BEGIN
    -- Update any aliases where we are the new ideal
    UPDATE tag_aliases
    SET ideal_alias_id = new.effective_tag_id
    WHERE effective_tag_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    -- update any parents where we are the new ideal
    UPDATE tag_parents
    SET ideal_parent_id = new.effective_tag_id
    WHERE parent_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    -- update any children where we are the new ideal
    UPDATE tag_parents
    SET ideal_child_id = new.effective_tag_id
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

CREATE OR REPLACE FUNCTION tag_aliases_before_insert_trigger()
    RETURNS TRIGGER
AS
$$
BEGIN

    -- set the ideal id to be the highest id of the chain
    new.ideal_alias_id = (SELECT fa.effective_tag_id -- COALESCE(fa.ideal_alias_id, fa.alias_id)
                          FROM tag_aliases fa
                          WHERE fa.aliased_id = new.alias_id
                            AND fa.tag_domain_id = new.tag_domain_id);

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- This trigger is for ensuring the ideal_alias_id is the highest of the chain
-- So if A->B is inserted and B->C exists already, A->(B)C, aliased_id -> (alias_id)ideal_alias_id will be created virtually
CREATE TRIGGER tag_aliases_before_insert
    BEFORE INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION tag_aliases_before_insert_trigger();