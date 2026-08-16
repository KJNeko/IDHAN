CREATE OR REPLACE FUNCTION delete_parents_from_active_mappings()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    -- 2.  Delete those that are no longer referenced
    DELETE
    FROM active_tag_mappings_parents
    WHERE record_id = old.record_id
      AND origin_id IN (old.tag_id, COALESCE(old.ideal_tag_id, old.tag_id))
      AND tag_domain_id = old.tag_domain_id
      AND NOT internal;

    RETURN old;
END;
$$;

CREATE TRIGGER trg_delete_parents_from_active_mappings
    AFTER DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION delete_parents_from_active_mappings();
