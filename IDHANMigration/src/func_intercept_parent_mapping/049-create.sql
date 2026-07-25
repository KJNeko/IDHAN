CREATE OR REPLACE FUNCTION intercept_parent_mapping()
    RETURNS trigger
    LANGUAGE plpgsql
    VOLATILE
AS
$$
BEGIN
    new.ideal_tag_id := (SELECT ta.effective_tag_id
                         FROM tag_aliases ta
                         WHERE ta.tag_domain_id = new.tag_domain_id
                           AND ta.aliased_id = new.tag_id);

    RETURN new;
END;
$$;

CREATE TRIGGER trg_intercept_parent_mapping
    BEFORE INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION intercept_parent_mapping();
