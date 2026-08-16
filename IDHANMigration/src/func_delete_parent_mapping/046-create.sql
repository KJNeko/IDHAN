CREATE OR REPLACE FUNCTION delete_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    -- Match against the ideal (alias-resolved) parent, since that is what is stored.
    DELETE
    FROM active_tag_mappings_parents
    WHERE tag_id = COALESCE(old.ideal_parent_id, old.parent_id)
      AND origin_id = old.child_id
      AND tag_domain_id = old.tag_domain_id;

    RETURN old;
END;
$$;

CREATE TRIGGER trg_delete_parent_mapping
    AFTER DELETE
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION delete_parent_mapping();
