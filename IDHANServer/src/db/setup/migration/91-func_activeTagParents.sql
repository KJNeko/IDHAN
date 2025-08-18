CREATE OR REPLACE FUNCTION insert_active_tag_mapping_parent()
    RETURNS TRIGGER AS
$$
BEGIN
    -- Insert the new parent mapping into active_tag_mappings_parents
    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
    SELECT record_id, new.parent_id, new.child_id, new.tag_domain_id
    FROM active_tag_mappings tm
    WHERE tm.tag_domain_id = new.tag_domain_id
      AND tm.tag_id = new.child_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create the trigger
CREATE TRIGGER trg_insert_active_tag_mapping_parent
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION insert_active_tag_mapping_parent();

CREATE OR REPLACE FUNCTION insert_active_tag_mappings_parents_from_mappings()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
    SELECT new.record_id, parent_id, new.tag_id, new.tag_domain_id
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.tag_id;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_insert_active_tag_mappings_parents_from_mappings
    AFTER INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION insert_active_tag_mappings_parents_from_mappings();

INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
SELECT tm.record_id, tp.parent_id, tp.child_id, tp.tag_domain_id
FROM active_tag_mappings tm
         JOIN tag_parents tp
              ON
                  COALESCE(tm.ideal_tag_id, tm.tag_id) = COALESCE(tp.ideal_child_id, tp.child_id)
                      AND tm.tag_domain_id = tp.tag_domain_id
ON CONFLICT DO NOTHING;