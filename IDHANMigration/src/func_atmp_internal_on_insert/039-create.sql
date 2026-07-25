-- =========================
-- Insert trigger function
-- =========================
-- DISTINCT guards against duplicate rows when several tag_parents entries resolve to the same
-- effective parent through alias resolution.
CREATE OR REPLACE FUNCTION atmp_internal_on_insert()
    RETURNS trigger
AS
$$
BEGIN

    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT DISTINCT new.record_id                              AS record_id,
                    COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
                    new.tag_id                                 AS origin_id,
                    new.tag_domain_id                          AS tag_domain_id,
                    1                                          AS internal_count
    FROM tag_parents tp
    WHERE COALESCE(tp.ideal_child_id, tp.child_id) = new.tag_id
      AND tp.tag_domain_id = new.tag_domain_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO UPDATE SET internal_count = excluded.internal_count + 1;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Create trigger for INSERTs on active_tag_mappings_parents
CREATE TRIGGER trg_atmp_internal_insert
    AFTER INSERT
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_insert();
