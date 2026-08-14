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

CREATE TRIGGER trg_insert_parent_mapping
    AFTER INSERT
    ON tag_parents
    FOR EACH ROW
EXECUTE FUNCTION insert_parent_mapping();
