-- tag_parents never resolved ideal_parent_id/ideal_child_id at insert time, unlike tag_aliases.
-- Backfill onto already-tagged records depended on whether the caller passed the ideal tag id.

CREATE OR REPLACE FUNCTION tag_parents_before_insert_trigger()
    RETURNS TRIGGER
AS
$$
BEGIN
    new.ideal_parent_id = (SELECT ta.effective_tag_id
                           FROM tag_aliases ta
                           WHERE ta.aliased_id = new.parent_id
                             AND ta.tag_domain_id = new.tag_domain_id);

    new.ideal_child_id = (SELECT ta.effective_tag_id
                          FROM tag_aliases ta
                          WHERE ta.aliased_id = new.child_id
                            AND ta.tag_domain_id = new.tag_domain_id);

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_tag_parents_before_insert
    BEFORE INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION tag_parents_before_insert_trigger();

-- Repair existing rows the above trigger never ran for.
UPDATE tag_parents tp
SET ideal_parent_id = ta.effective_tag_id
FROM tag_aliases ta
WHERE ta.aliased_id = tp.parent_id
  AND ta.tag_domain_id = tp.tag_domain_id
  AND tp.ideal_parent_id IS NULL;

UPDATE tag_parents tp
SET ideal_child_id = ta.effective_tag_id
FROM tag_aliases ta
WHERE ta.aliased_id = tp.child_id
  AND ta.tag_domain_id = tp.tag_domain_id
  AND tp.ideal_child_id IS NULL;

-- Previously matched on two separate raw-id branches, so a tag_parents relationship defined via
-- a synonym other than the one actually applied was invisible to it. Match effective-to-effective
-- instead, mirroring insert_parent_mapping (migration 174).
CREATE OR REPLACE FUNCTION insert_parents_from_active_mappings()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    INSERT INTO active_tag_mappings_parents
        (record_id, tag_id, origin_id, tag_domain_id)
    SELECT new.record_id                              AS record_id,
           COALESCE(tp.ideal_parent_id, tp.parent_id) AS tag_id,
           COALESCE(new.ideal_tag_id, new.tag_id)      AS origin_id,
           new.tag_domain_id                           AS tag_domain_id
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND COALESCE(tp.ideal_child_id, tp.child_id) = COALESCE(new.ideal_tag_id, new.tag_id)
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO NOTHING;

    RETURN new;
END;
$$;

-- Rebuild active_tag_mappings_parents from scratch: prior rows could be silently missing rather
-- than mislabeled. Triggers stay enabled, so trg_atmp_internal_insert (migration 91/170)
-- recursively fills in every ancestor level with correct internal_count bookkeeping, same as a
-- fresh tagging event would.
TRUNCATE active_tag_mappings_parents;

INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id)
SELECT DISTINCT atm.record_id,
                COALESCE(tp.ideal_parent_id, tp.parent_id),
                COALESCE(atm.ideal_tag_id, atm.tag_id),
                atm.tag_domain_id
FROM active_tag_mappings atm
         JOIN tag_parents tp
              ON tp.tag_domain_id = atm.tag_domain_id
                  AND COALESCE(tp.ideal_child_id, tp.child_id) = COALESCE(atm.ideal_tag_id, atm.tag_id)
ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO NOTHING;
