CREATE OR REPLACE FUNCTION repair_tag_mappings_ideals_update()
    RETURNS TRIGGER AS
$$
BEGIN
    UPDATE active_tag_mappings
    SET ideal_tag_id = new.effective_tag_id
    WHERE tag_id = old.aliased_id
      AND ideal_tag_id = old.effective_tag_id
      AND tag_domain_id = new.tag_domain_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_repair_tag_mappings_ideals_update
    AFTER UPDATE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_update();
