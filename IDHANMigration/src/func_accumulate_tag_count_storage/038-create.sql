CREATE OR REPLACE FUNCTION accumulate_tag_count_storage() RETURNS TRIGGER AS
$$
BEGIN

    CASE tg_op
        WHEN 'INSERT' THEN PERFORM add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id);

        WHEN 'UPDATE' THEN PERFORM remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id);
                           PERFORM add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id);

        WHEN 'DELETE' THEN PERFORM remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id);

        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER accumulate_tag_count_trigger
    AFTER INSERT OR UPDATE OR DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE PROCEDURE accumulate_tag_count_storage();
