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

CREATE TRIGGER tag_aliases_before_insert
    BEFORE INSERT
    ON tag_aliases
    FOR EACH ROW
EXECUTE FUNCTION tag_aliases_before_insert_trigger();
