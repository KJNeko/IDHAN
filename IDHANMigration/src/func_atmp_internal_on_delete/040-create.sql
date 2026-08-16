-- =========================
-- Delete trigger function
-- =========================
CREATE OR REPLACE FUNCTION atmp_internal_on_delete()
    RETURNS trigger
AS
$$
BEGIN

    UPDATE active_tag_mappings_parents
    SET internal_count = internal_count - 1
    WHERE internal_count > 0
      AND record_id = old.record_id
      AND origin_id = old.tag_id
      AND tag_domain_id = old.tag_domain_id;

    DELETE
    FROM active_tag_mappings_parents
    WHERE record_id = old.record_id
      AND origin_id = old.tag_id
      AND tag_domain_id = old.tag_domain_id
      AND internal_count = 0;

    RETURN old;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Create trigger for DELETEs on active_tag_mappings_parents
CREATE TRIGGER trg_atmp_internal_delete
    AFTER DELETE
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_delete();
