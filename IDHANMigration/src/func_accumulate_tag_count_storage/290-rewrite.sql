CREATE OR REPLACE FUNCTION accumulate_tag_count_storage() RETURNS TRIGGER AS
$$
BEGIN

    CASE tg_op
        WHEN 'INSERT' THEN PERFORM add_count(new.tag_id, new.ideal_tag_id, new.tag_domain_id);

        WHEN 'DELETE' THEN PERFORM remove_count(old.tag_id, old.ideal_tag_id, old.tag_domain_id);

        WHEN 'UPDATE' THEN PERFORM remove_count(NULL, COALESCE(old.ideal_tag_id, old.tag_id), old.tag_domain_id);
                           PERFORM add_count(NULL, COALESCE(new.ideal_tag_id, new.tag_id), new.tag_domain_id);

        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE TRIGGER accumulate_tag_count_trigger
    AFTER INSERT OR DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION accumulate_tag_count_storage();

CREATE OR REPLACE TRIGGER accumulate_tag_count_update_trigger
    AFTER UPDATE
    ON active_tag_mappings
    FOR EACH ROW
    WHEN ( COALESCE(old.ideal_tag_id, old.tag_id) IS DISTINCT FROM COALESCE(new.ideal_tag_id, new.tag_id) )
EXECUTE FUNCTION accumulate_tag_count_storage();
