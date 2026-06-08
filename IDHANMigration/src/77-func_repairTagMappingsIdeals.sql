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

-- Create triggers to execute the functions
DROP TRIGGER IF EXISTS trg_repair_tag_mappings_ideals_insert ON tag_aliases;
CREATE TRIGGER trg_repair_tag_mappings_ideals_insert
    AFTER INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_insert();

DROP TRIGGER IF EXISTS trg_repair_tag_mappings_ideals_update ON tag_aliases;
CREATE TRIGGER trg_repair_tag_mappings_ideals_update
    AFTER UPDATE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_update();

DROP TRIGGER IF EXISTS trg_repair_tag_mappings_ideals_delete ON tag_aliases;
CREATE TRIGGER trg_repair_tag_mappings_ideals_delete
    AFTER DELETE
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION repair_tag_mappings_ideals_delete();