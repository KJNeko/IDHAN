-- =========================
-- Insert trigger function
-- =========================
CREATE OR REPLACE FUNCTION atmp_internal_on_insert()
    RETURNS trigger
AS
$$

DECLARE
    row RECORD;
BEGIN

    --     FOR new IN SELECT * FROM new_rows
--         LOOP
    -- Process each new row here
    RAISE NOTICE 'Inserted: %', new;

    RAISE NOTICE 'Count: %', (SELECT COUNT(*) FROM tag_parents tp2);

    FOR row IN SELECT * FROM tag_parents tp2
        LOOP
            RAISE NOTICE 'TP Row: %', row;
        END LOOP;

--     INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
    SELECT new.record_id                              AS record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
           new.tag_id                                 AS origin_id,
           new.tag_domain_id                          AS tag_domain_id
    INTO row
    FROM tag_parents tp
    WHERE COALESCE(tp.ideal_child_id, tp.child_id) = new.tag_id
      AND tp.tag_domain_id = new.tag_domain_id
    LIMIT 1;

    RAISE NOTICE 'INSERT: %', row;

--         END LOOP;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- =========================
-- Delete trigger function
-- =========================
CREATE OR REPLACE FUNCTION atmp_internal_on_delete()
    RETURNS trigger
AS
$$
BEGIN


    RETURN new;
END;
$$ LANGUAGE plpgsql;

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