
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
