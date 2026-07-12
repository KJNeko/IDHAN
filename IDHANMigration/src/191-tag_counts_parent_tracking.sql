-- Parent-only tags (never directly applied or aliased-to) never got a tag_counts row, since
-- accumulate_tag_count_trigger (migration 93) only watches active_tag_mappings. Mirrors that
-- trigger's exact add_count/remove_count semantics, applied to active_tag_mappings_parents.

CREATE OR REPLACE FUNCTION accumulate_tag_count_parents()
    RETURNS TRIGGER AS
$$
BEGIN
    CASE tg_op
        WHEN 'INSERT' THEN PERFORM add_count( NULL, new.tag_id, new.tag_domain_id );
        WHEN 'DELETE' THEN PERFORM remove_count( NULL, old.tag_id, old.tag_domain_id );
        END CASE;

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_accumulate_tag_count_parents
    AFTER INSERT OR DELETE
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION accumulate_tag_count_parents();

-- Full recompute rather than incremental patch, run after migration 189's
-- active_tag_mappings_parents rebuild so the parent-count seed reflects corrected data.
TRUNCATE tag_counts;

SELECT add_count( tag_id, ideal_tag_id, tag_domain_id )
FROM active_tag_mappings;

SELECT add_count( NULL, tag_id, tag_domain_id )
FROM active_tag_mappings_parents;
