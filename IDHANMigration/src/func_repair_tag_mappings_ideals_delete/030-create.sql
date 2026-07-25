CREATE OR REPLACE FUNCTION repair_tag_mappings_ideals_delete()
    RETURNS TRIGGER AS
$$
BEGIN
    UPDATE active_tag_mappings
    SET ideal_tag_id = NULL
    WHERE tag_id = old.aliased_id
      AND tag_domain_id = old.tag_domain_id;

    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_repair_tag_mappings_ideals_delete
    AFTER DELETE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_delete();
