CREATE OR REPLACE FUNCTION intercept_active_tag_mappings_parents_sibling()
    RETURNS TRIGGER
AS
$$
DECLARE
    oldest_sibling_id INTEGER;
BEGIN
    oldest_sibling_id := (SELECT COALESCE(ta.effective_tag_id,
                                          COALESCE(ts.ideal_older_id, ts.older_id))
                          FROM tag_siblings ts
                                   LEFT JOIN tag_aliases ta
                                             ON ta.aliased_id = COALESCE(ts.ideal_older_id, ts.older_id)
                                                 AND ta.tag_domain_id = ts.tag_domain_id
                          WHERE ts.younger_id = new.tag_id
                            AND ts.tag_domain_id = new.tag_domain_id);

    IF oldest_sibling_id IS NOT NULL THEN
        new.tag_id := oldest_sibling_id;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

DROP TRIGGER IF EXISTS trg_intercept_active_tag_mappings_parents_sibling ON active_tag_mappings_parents;
CREATE TRIGGER trg_intercept_active_tag_mappings_parents_sibling
    BEFORE INSERT
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION intercept_active_tag_mappings_parents_sibling();
