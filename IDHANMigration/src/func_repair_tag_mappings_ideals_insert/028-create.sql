CREATE OR REPLACE FUNCTION repair_tag_mappings_ideals_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    UPDATE active_tag_mappings
    SET ideal_tag_id = new.effective_tag_id
    WHERE tag_id = new.aliased_id
      AND tag_domain_id = new.tag_domain_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_repair_tag_mappings_ideals_insert
    AFTER INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_insert();
