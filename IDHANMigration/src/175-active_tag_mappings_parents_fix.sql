-- Fix: insert_parents_from_active_mappings first INSERT used
-- `excluded.internal_count + 1` on conflict, but excluded.internal_count is
-- always 0 (the constant inserted), so every conflict reset internal_count to 1
-- regardless of the existing value.
--
-- Correct form: `active_tag_mappings_parents.internal_count + 1`, which
-- preserves the existing count as the second INSERT branch already did.

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
        DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + 1;

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
