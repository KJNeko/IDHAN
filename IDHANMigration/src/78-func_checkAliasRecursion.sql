CREATE OR REPLACE FUNCTION check_alias_recursion()
    RETURNS TRIGGER AS
$$
BEGIN

    -- Check for direct circular reference
    IF (SELECT 1
        FROM tag_aliases a
        WHERE a.effective_tag_id = new.aliased_id
          AND a.aliased_id = new.effective_tag_id
          AND a.tag_domain_id = new.tag_domain_id)
    THEN
        RAISE EXCEPTION 'Circular reference detected between aliases';
    END IF;
    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create trigger for the tag_aliasess table
CREATE TRIGGER tr_check_alias_recursion
    BEFORE INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION check_alias_recursion();