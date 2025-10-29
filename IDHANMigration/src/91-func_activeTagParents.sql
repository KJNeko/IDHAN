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

    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT DISTINCT atmp.record_id                                as record_id,
                    new.parent_id                                 as tag_id,
                    new.child_id                                  as origin_id,
                    new.tag_domain_id                             as tag_domain_id,
                    (SELECT COUNT(*)
                     FROM active_tag_mappings_parents atmp_count
                     WHERE atmp_count.tag_domain_id = new.tag_domain_id
                       AND atmp_count.tag_id = new.child_id
                       AND atmp_count.record_id = atmp.record_id) AS internal_count
    FROM active_tag_mappings_parents atmp
    WHERE atmp.tag_domain_id = new.tag_domain_id
      AND atmp.tag_id = new.child_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO UPDATE SET internal_count = excluded.internal_count + 1;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Create the trigger
CREATE TRIGGER trg_insert_active_tag_mapping_parent
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION insert_active_tag_mapping_parent();

CREATE OR REPLACE FUNCTION delete_active_tag_mapping_parent()
    RETURNS TRIGGER AS
$$
BEGIN
    -- Delete the corresponding parent mappings from active_tag_mappings_parents
    DELETE
    FROM active_tag_mappings_parents
    WHERE tag_id = old.parent_id
      AND origin_id = old.child_id
      AND tag_domain_id = old.tag_domain_id;

    RETURN old;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Create the trigger for deletion
CREATE TRIGGER trg_delete_active_tag_mapping_parent
    AFTER DELETE
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION delete_active_tag_mapping_parent();

CREATE OR REPLACE FUNCTION insert_active_tag_mappings_parents_from_mappings()
    RETURNS TRIGGER AS
$$
BEGIN
    -- whenever a mapping is inserted into the active mappings
    -- insert it's parents as well

    INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT new.record_id     as record_id,
           parent_id         as tag_id,
           new.tag_id        as origin_id,
           new.tag_domain_id as tag_domain_id,
           0                 AS internal_count
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.tag_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO UPDATE SET internal_count = excluded.internal_count + 1;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

CREATE TRIGGER trg_insert_active_tag_mappings_parents_from_mappings
    AFTER INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION insert_active_tag_mappings_parents_from_mappings();

CREATE OR REPLACE FUNCTION delete_active_tag_mappings_parents_from_mappings()
    RETURNS TRIGGER AS
$$
BEGIN
    UPDATE active_tag_mappings_parents
    SET internal = TRUE
    WHERE record_id = old.record_id
      AND origin_id = old.tag_id
      AND tag_domain_id = old.tag_domain_id
      AND internal_count > 0;

    -- When a mapping is deleted from active_tag_mappings
    -- remove its corresponding parent mappings
    DELETE
    FROM active_tag_mappings_parents
    WHERE record_id = old.record_id
      AND origin_id = old.tag_id
      AND tag_domain_id = old.tag_domain_id
      AND NOT internal;

    RETURN old;
END;
$$ LANGUAGE plpgsql VOLATILE;

CREATE TRIGGER trg_delete_active_tag_mappings_parents_from_mappings
    AFTER DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION delete_active_tag_mappings_parents_from_mappings();

CREATE OR REPLACE FUNCTION intercept_active_tag_mappings_parents()
    RETURNS TRIGGER AS
$$
BEGIN
    new.ideal_tag_id = (SELECT ta.effective_tag_id
                        FROM tag_aliases ta
                        WHERE ta.tag_domain_id = new.tag_domain_id
                          AND ta.aliased_id = new.tag_id);

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

CREATE TRIGGER trg_intercept_active_tag_mappings_parents
    BEFORE INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION intercept_active_tag_mappings_parents();