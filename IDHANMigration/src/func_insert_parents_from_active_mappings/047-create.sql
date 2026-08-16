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
           COALESCE(new.ideal_tag_id, new.tag_id)     AS origin_id,
           new.tag_domain_id                          AS tag_domain_id
    FROM tag_parents tp
    WHERE tp.tag_domain_id = new.tag_domain_id
      AND COALESCE(tp.ideal_child_id, tp.child_id) = COALESCE(new.ideal_tag_id, new.tag_id)
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO NOTHING;

    RETURN new;
END;
$$;

CREATE TRIGGER trg_insert_parents_from_active_mappings
    AFTER INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION insert_parents_from_active_mappings();
