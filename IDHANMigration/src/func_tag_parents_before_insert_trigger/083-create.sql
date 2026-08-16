-- tag_parents never resolved ideal_parent_id/ideal_child_id at insert time, unlike tag_aliases.
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
