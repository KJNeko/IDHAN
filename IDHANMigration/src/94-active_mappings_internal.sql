-- =========================
-- Insert trigger function
-- =========================
CREATE OR REPLACE FUNCTION atmp_internal_on_insert()
    RETURNS trigger
AS
$$
BEGIN

    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal, internal_count)
    SELECT new.record_id                              AS record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
           new.tag_id                                 AS origin_id,
           new.tag_domain_id                          AS tag_domain_id,
           TRUE                                       AS internal,
           1                                          AS internal_count
    FROM tag_parents tp
    WHERE COALESCE(tp.ideal_child_id, tp.child_id) = new.tag_id
      AND tp.tag_domain_id = new.tag_domain_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO UPDATE SET internal_count = excluded.internal_count + 1;

    -- if internal is true and there is an internal count, that means there are only internal tags
    -- if internal is false but the count is non-zero, that means we have a direct parent
    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

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

    DELETE FROM active_tag_mappings_parents WHERE internal AND internal_count = 0;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- =========================
-- Triggers
-- =========================

-- Create trigger for INSERTs on active_tag_mappings_parents
DROP TRIGGER IF EXISTS trg_atmp_internal_insert ON active_tag_mappings_parents;
CREATE TRIGGER trg_atmp_internal_insert
    AFTER INSERT
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_insert();

-- Create trigger for DELETEs on active_tag_mappings_parents
DROP TRIGGER IF EXISTS trg_atmp_internal_delete ON active_tag_mappings_parents;
CREATE TRIGGER trg_atmp_internal_delete
    AFTER DELETE
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_delete();