-- Replaces the before-insert trigger from migration 73 to reject alias cycles with a
-- controlled message. Previously a cycle was only caught by the unnamed
-- CHECK ( aliased_id != ideal_alias_id ) on tag_aliases, whose error text is
-- PostgreSQL's own (localized and auto-named), which the API cannot match reliably.
-- The 'Cycle detected' prefix matches the convention of check_parent_cycle (migration 105)
-- and is matched by the createTagAliases endpoint to return 409. The CHECK remains as a backstop.
CREATE OR REPLACE FUNCTION tag_aliases_before_insert_trigger()
    RETURNS TRIGGER
AS
$$
BEGIN

    -- set the ideal id to be the highest id of the chain
    new.ideal_alias_id = (SELECT fa.effective_tag_id
                          FROM tag_aliases fa
                          WHERE fa.aliased_id = new.alias_id
                            AND fa.tag_domain_id = new.tag_domain_id);

    -- the chain starting at the new alias target ends back at the aliased tag:
    -- inserting this row would close a loop
    IF new.ideal_alias_id = new.aliased_id THEN
        RAISE EXCEPTION 'Cycle detected: aliasing % to % would create an alias cycle in domain %',
            new.aliased_id, new.alias_id, new.tag_domain_id;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql;
