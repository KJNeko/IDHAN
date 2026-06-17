-- Fix: active_tag_mappings_parents.tag_id was being stored as the raw parent_id
-- from tag_parents rather than the alias-resolved (ideal) parent.  This caused
-- searches and listActiveTags to expose un-aliased parent tag IDs.
--
-- The correct value is COALESCE(ideal_parent_id, parent_id), mirroring what
-- atmp_internal_on_insert (migration 170) already does correctly.
--
-- Three trigger functions are replaced:
--   insert_parent_mapping        — stores ideal parent; matches child by effective tag
--   delete_parent_mapping        — deletes by ideal parent (matches new storage)
--   insert_parents_from_active_mappings — stores ideal parent for both original and aliased branches
--
-- A data-repair block follows to fix any existing rows with raw parent IDs.
-- The view is also updated to COALESCE on the parents branch as defence-in-depth.

-- =============================================================================
-- Trigger functions
-- =============================================================================

CREATE OR REPLACE FUNCTION insert_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    -- 1.  Insert the direct mapping using the ideal (alias-resolved) parent.
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id)
    SELECT tm.record_id                                 AS record_id,
           COALESCE(new.ideal_parent_id, new.parent_id) AS tag_id,
           new.child_id                                 AS origin_id,
           new.tag_domain_id                            AS tag_domain_id
    FROM active_tag_mappings tm
    WHERE tm.tag_domain_id = new.tag_domain_id
      AND COALESCE(tm.ideal_tag_id, tm.tag_id) = COALESCE(new.ideal_child_id, new.child_id)
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO NOTHING;

    -- 2.  Propagate the new parent up through any existing ancestor parent entries.
    WITH ancestor AS (SELECT atmp.record_id                               AS record_id,
                             COALESCE(new.ideal_parent_id, new.parent_id) AS tag_id,
                             new.child_id                                 AS origin_id,
                             new.tag_domain_id                            AS tag_domain_id
                      FROM active_tag_mappings_parents atmp
                      WHERE atmp.tag_domain_id = new.tag_domain_id
                        AND atmp.tag_id = COALESCE(new.ideal_child_id, new.child_id))
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
    -- Match against the ideal parent since that is now what is stored.
    DELETE
    FROM active_tag_mappings_parents
    WHERE tag_id = COALESCE(old.ideal_parent_id, old.parent_id)
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
    -- Parents of the original tag; origin = raw tag_id.
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT new.record_id                              AS record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
           new.tag_id                                 AS origin_id,
           new.tag_domain_id                          AS tag_domain_id,
           0                                          AS internal_count
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.tag_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = excluded.internal_count + 1;

    -- Parents of the alias target when the tag is aliased; origin = ideal_tag_id.
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT new.record_id                              AS record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
           new.ideal_tag_id                           AS origin_id,
           new.tag_domain_id                          AS tag_domain_id,
           1                                          AS internal_count
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND tp.child_id = new.ideal_tag_id
      AND new.ideal_tag_id IS DISTINCT FROM new.tag_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + 1;

    RETURN new;
END;
$$;

-- =============================================================================
-- Data repair
-- Rows in active_tag_mappings_parents whose tag_id is an aliased (non-ideal)
-- tag are deleted and reinserted with the effective (ideal) tag_id.
-- Counts are summed when the repaired row conflicts with an existing ideal row.
-- =============================================================================

WITH to_fix AS (
    DELETE FROM active_tag_mappings_parents atmp
        WHERE EXISTS (SELECT 1
                      FROM tag_aliases ta
                      WHERE ta.aliased_id = atmp.tag_id
                        AND ta.tag_domain_id = atmp.tag_domain_id)
        RETURNING record_id, tag_id, origin_id, tag_domain_id, internal_count)
INSERT
INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
SELECT tf.record_id,
       ta.effective_tag_id         AS tag_id,
       tf.origin_id,
       tf.tag_domain_id,
       SUM(tf.internal_count)::int AS internal_count
FROM to_fix tf
         JOIN tag_aliases ta ON ta.aliased_id = tf.tag_id AND ta.tag_domain_id = tf.tag_domain_id
GROUP BY tf.record_id, ta.effective_tag_id, tf.origin_id, tf.tag_domain_id
ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
    DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + excluded.internal_count;

-- =============================================================================
-- View: defence-in-depth alias resolution on the parents branch
-- =============================================================================

CREATE OR REPLACE VIEW active_tag_mappings_final AS
(
SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id, tag_domain_id
FROM active_tag_mappings
UNION ALL
SELECT atmp.record_id,
       COALESCE(ta.effective_tag_id, atmp.tag_id) AS tag_id,
       atmp.tag_domain_id
FROM active_tag_mappings_parents atmp
         LEFT JOIN tag_aliases ta ON ta.aliased_id = atmp.tag_id AND ta.tag_domain_id = atmp.tag_domain_id);
