CREATE OR REPLACE FUNCTION check_parent_cycle()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    IF EXISTS (WITH RECURSIVE ancestors AS (SELECT tp.parent_id, tp.tag_domain_id
                                            FROM tag_parents tp
                                            WHERE tp.child_id = NEW.parent_id
                                              AND tp.tag_domain_id = NEW.tag_domain_id
                                            UNION
                                            SELECT tp.parent_id, tp.tag_domain_id
                                            FROM tag_parents tp
                                                     INNER JOIN ancestors a
                                                                ON tp.child_id = a.parent_id AND tp.tag_domain_id = a.tag_domain_id)
               SELECT 1
               FROM ancestors
               WHERE parent_id = NEW.child_id) THEN
        RAISE EXCEPTION 'Cycle detected: inserting parent % for child % would create a cycle in domain %',
            NEW.parent_id, NEW.child_id, NEW.tag_domain_id;
    END IF;
    RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION insert_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    -- 1.  Insert the direct mapping (parent → child)
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id)
    SELECT tm.record_id      as record_id,
           new.parent_id     as parent_id,
           new.child_id      as child_id,
           new.tag_domain_id as tag_domain_id
    FROM active_tag_mappings tm
    WHERE tm.tag_domain_id = new.tag_domain_id
      AND (tm.tag_id = new.child_id OR COALESCE(tm.ideal_tag_id, tm.tag_id) = new.child_id)
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO NOTHING;
    -- the PK guarantees uniqueness

    -- 2.  Insert all ancestor parents of the child, and aggregate counts
    WITH ancestor AS (SELECT atmp.record_id    AS record_id,
                             new.parent_id     AS tag_id,
                             new.child_id AS origin_id,
                             new.tag_domain_id AS tag_domain_id
                      FROM active_tag_mappings_parents atmp
                      WHERE atmp.tag_domain_id = new.tag_domain_id
                        AND atmp.tag_id = new.child_id)
    INSERT
    INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT record_id,
           tag_id,
           origin_id,
           tag_domain_id,
           COUNT(*)::int AS internal_count
    FROM ancestor
    GROUP BY record_id, tag_id, origin_id, tag_domain_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + excluded.internal_count;

    RETURN new;
END;
$$;

CREATE OR REPLACE FUNCTION delete_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    DELETE
    FROM active_tag_mappings_parents
    WHERE tag_id = old.parent_id
      AND origin_id = old.child_id
      AND tag_domain_id = old.tag_domain_id;

    RETURN old;
END;
$$;

CREATE OR REPLACE FUNCTION insert_parents_from_active_mappings()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    -- Parents of the original tag (if any): origin = original tag
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT new.record_id     as record_id,
           tp.parent_id      as tag_id,
           new.tag_id        as origin_id,
           new.tag_domain_id as tag_domain_id,
           0                 AS internal_count
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.tag_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = excluded.internal_count + 1;

    -- Parents of the alias target (if different from original): origin = alias target
    -- Use internal_count=1 since this is a virtual propagation through an alias
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT new.record_id     as record_id,
           tp.parent_id      as tag_id,
           new.ideal_tag_id  as origin_id,
           new.tag_domain_id as tag_domain_id,
           1                 AS internal_count
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.ideal_tag_id
      AND new.ideal_tag_id IS DISTINCT FROM new.tag_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + 1;

    RETURN new;
END;
$$;

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

CREATE OR REPLACE FUNCTION intercept_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    new.ideal_tag_id := (SELECT ta.effective_tag_id
                         FROM tag_aliases ta
                         WHERE ta.tag_domain_id = new.tag_domain_id
                           AND ta.aliased_id = new.tag_id);

    RETURN new;
END;
$$;

DROP TRIGGER trg_delete_active_tag_mapping_parent ON tag_parents;
DROP TRIGGER trg_insert_active_tag_mapping_parent ON tag_parents;

DROP TRIGGER trg_delete_active_tag_mappings_parents_from_mappings ON active_tag_mappings;
DROP TRIGGER trg_insert_active_tag_mappings_parents_from_mappings ON active_tag_mappings;
DROP TRIGGER trg_intercept_active_tag_mappings_parents ON active_tag_mappings;

CREATE TRIGGER trg_check_parent_cycle
    BEFORE INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION check_parent_cycle();

CREATE TRIGGER trg_insert_parent_mapping
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION insert_parent_mapping();

CREATE TRIGGER trg_delete_parent_mapping
    AFTER DELETE
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION delete_parent_mapping();

CREATE TRIGGER trg_insert_parents_from_active_mappings
    AFTER INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION insert_parents_from_active_mappings();

CREATE TRIGGER trg_delete_parents_from_active_mappings
    AFTER DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION delete_parents_from_active_mappings();

CREATE TRIGGER trg_intercept_parent_mapping
    BEFORE INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION intercept_parent_mapping();