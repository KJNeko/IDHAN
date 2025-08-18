CREATE OR REPLACE FUNCTION repair_tag_mappings_ideals()
    RETURNS TRIGGER AS
$$
BEGIN

    CASE tg_op

        -- On INSERT or UPDATE: Recalculate mappings for affected tags
        WHEN 'INSERT' THEN UPDATE active_tag_mappings
                           SET ideal_tag_id = COALESCE(new.ideal_alias_id, new.alias_id)
                           WHERE tag_id = new.aliased_id
                             AND tag_domain_id = new.tag_domain_id;
        WHEN 'UPDATE' THEN UPDATE active_tag_mappings
                           SET ideal_tag_id = COALESCE(new.ideal_alias_id, new.alias_id)
                           WHERE tag_id = old.aliased_id
                             AND ideal_tag_id = COALESCE(old.ideal_alias_id, old.alias_id)
                             AND tag_domain_id = new.tag_domain_id;
        -- On DELETE: Remove mappings that were using the deleted alias
        WHEN 'DELETE' THEN UPDATE active_tag_mappings
                           SET ideal_tag_id = NULL
                           WHERE tag_id = old.aliased_id
                             AND tag_domain_id = old.tag_domain_id;
        END CASE;

    RETURN COALESCE(new, old);
END;
$$ LANGUAGE plpgsql;

-- Create trigger to execute the function
DROP TRIGGER IF EXISTS trg_repair_tag_mappings_ideals ON tag_aliases;
CREATE TRIGGER trg_repair_tag_mappings_ideals
    AFTER INSERT OR UPDATE OR DELETE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals();