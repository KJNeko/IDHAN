CREATE OR REPLACE FUNCTION update_tag_mappings_aliases()
    RETURNS TRIGGER AS
$$
BEGIN
    CASE
        WHEN tg_op = 'INSERT' THEN -- Apply the ideal tag to any aliased tag
        RAISE DEBUG 'Alias inserted, Converting "%" to "%"', tag_text(new.aliased_id), tag_text(new.alias_id);
        UPDATE tag_mappings SET ideal_tag_id = new.alias_id WHERE domain_id = new.domain_id AND tag_id = new.aliased_id;

        WHEN tg_op = 'UPDATE' THEN -- Fix any invalid tags
        RAISE DEBUG 'Alias converting "%" to "%" updated to "%" -> "%", Setting tags overwriten by that to new ideal', tag_text(old.aliased_id), tag_text(old.alias_id), tag_text(new.aliased_id), tag_text(new.alias_id);
        -- Old will be what we need to fix, Change it to new instead
        UPDATE tag_mappings SET ideal_tag_id = new.alias_id WHERE domain_id = new.domain_id AND tag_id = new.aliased_id;

        WHEN tg_op = 'DELETE' THEN -- Set tag back to it's old id
        RAISE DEBUG 'Alias converting % (%) to % (%) deleted, Setting tags overwriten by that to new ideal', tag_text(old.aliased_id), old.aliased_id, tag_text(old.alias_id), old.alias_id;

        UPDATE tag_mappings
        SET ideal_tag_id = (SELECT fa.alias_id FROM flattened_aliases fa WHERE fa.domain_id = new.domain_id AND fa.aliased_id = tag_mappings.tag_id)
        WHERE domain_id = old.domain_id
          AND ideal_tag_id = old.alias_id;
        END CASE;
    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

CREATE OR REPLACE TRIGGER update_tag_mappings_from_aliases
    AFTER INSERT OR UPDATE OR DELETE
    ON flattened_aliases
    FOR EACH ROW
EXECUTE FUNCTION update_tag_mappings_aliases();

CREATE FUNCTION wrap_tag_mappings_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    new.ideal_tag_id = (SELECT alias_id FROM flattened_aliases WHERE aliased_id = new.tag_id AND domain_id = new.domain_id);

    IF new.ideal_tag_id IS NOT NULL THEN
        RAISE DEBUG 'Set ideal tag to (%) "%" for tag (%) "%" on record % in domain %', new.ideal_tag_id, tag_text(new.ideal_tag_id), new.tag_id, tag_text(new.tag_id), new.record_id, new.domain_id;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql STABLE;

CREATE TRIGGER insert_tag_mappings_intercept
    BEFORE INSERT
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION wrap_tag_mappings_insert();