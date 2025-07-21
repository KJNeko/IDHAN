CREATE TABLE tag_mappings_virtuals
(
    record_id INTEGER  NOT NULL REFERENCES records (record_id),
    origin_id INTEGER  NOT NULL REFERENCES tags (tag_id),
    tag_id    INTEGER  NOT NULL REFERENCES tags (tag_id),
    domain_id SMALLINT NOT NULL REFERENCES tag_domains (tag_domain_id),
    UNIQUE (record_id, tag_id)
);

CREATE FUNCTION tag_virtuals_mappings_update_trigger()
    RETURNS TRIGGER AS
$$
BEGIN
    IF old.ideal_tag_id = new.ideal_tag_id THEN
        -- Ideal was not changed, This is abnormal
        RAISE EXCEPTION 'tag_mapping should only be updated through the ideal_tag_id, Which it was not';
    END IF;

    -- This means the ideal id got changed
    IF new.ideal_tag_id IS NULL THEN
        RAISE DEBUG 'Updating tag_id for % to %', old.ideal_tag_id, new.ideal_tag_id;
        -- The new ideal is null, Meaning the chain has been wiped. So we should update anything with the origin_id to return back to the origin_id
        UPDATE tag_mappings_virtuals SET tag_id = origin_id WHERE tag_id = old.ideal_tag_id AND record_id = old.record_id;
    ELSE
        IF new.ideal_tag_id = old.ideal_tag_id THEN
            RAISE EXCEPTION 'The old id was the same as the new id!';
        END IF;

        RAISE DEBUG 'Updating tag_id for % to % due to alias change', old.ideal_tag_id, new.ideal_tag_id;
        UPDATE tag_mappings_virtuals SET tag_id = new.ideal_tag_id WHERE tag_id = old.ideal_tag_id AND record_id = old.record_id;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION tag_virtuals_mappings_insert_trigger()
    RETURNS TRIGGER AS
$$
BEGIN
    RAISE DEBUG 'Inserting new tag_mapping % into tag_mappings_virtuals due to new tag %', new.record_id, new.tag_id;
    INSERT INTO tag_mappings_virtuals (record_id, tag_id, origin_id, domain_id)
    SELECT DISTINCT new.record_id, ap.parent_id AS tag_id, COALESCE(new.ideal_tag_id, new.tag_id) AS origin_id, new.domain_id
    FROM aliased_parents ap
    WHERE ap.child_id = COALESCE(new.ideal_tag_id, new.tag_id)
      AND ap.domain_id = new.domain_id
    ON CONFLICT DO NOTHING;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION tag_virtuals_mappings_delete_trigger()
    RETURNS TRIGGER AS
$$
BEGIN
    DELETE FROM tag_mappings_virtuals WHERE record_id = old.record_id AND tag_id = old.tag_id;
    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tag_virtuals_after_mappings_insert
    AFTER INSERT
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_mappings_insert_trigger();

CREATE TRIGGER tag_virtuals_after_mappings_update
    AFTER UPDATE
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_mappings_update_trigger();

CREATE TRIGGER tag_virtuals_after_mappings_delete
    AFTER DELETE
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_mappings_delete_trigger();

CREATE INDEX ON tag_mappings (domain_id, COALESCE(ideal_tag_id, tag_id));

CREATE FUNCTION tag_virtuals_after_insert_aliased_parents()
    RETURNS TRIGGER AS
$$
BEGIN
    RAISE DEBUG 'Inserting new parent % into tag_mappings_virtuals due to child %', COALESCE(new.parent_id, new.original_parent_id), COALESCE(new.child_id, new.original_child_id);

    INSERT INTO tag_mappings_virtuals (record_id, origin_id, tag_id, domain_id)
    SELECT DISTINCT tm.record_id, COALESCE(tm.ideal_tag_id, tm.tag_id) AS origin_id, COALESCE(new.parent_id, new.original_parent_id) AS tag_id, tm.domain_id
    FROM tag_mappings tm
             JOIN aliased_parents ap ON tm.domain_id = ap.domain_id AND COALESCE(tm.ideal_tag_id, tm.tag_id) = COALESCE(ap.child_id, ap.original_child_id)
    WHERE COALESCE(tm.ideal_tag_id, tm.tag_id) = COALESCE(new.child_id, new.original_child_id)
      AND tm.domain_id = new.domain_id
    ON CONFLICT
        DO NOTHING;

    -- This gets executed whenever something new is inserted into aliased_parents
    INSERT INTO tag_mappings_virtuals (record_id, origin_id, tag_id, domain_id, self_origin)
    SELECT tm.record_id, COALESCE(new.child_id, new.original_child_id) AS origin_id, COALESCE(new.parent_id, new.original_parent_id) AS tag_id, tm.domain_id, TRUE AS self_origin
    FROM tag_mappings_virtuals tm
    WHERE tm.tag_id = COALESCE(new.child_id, new.original_child_id)
      AND tm.domain_id = new.domain_id
    ON CONFLICT DO NOTHING;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION tag_virtuals_after_update_aliased_parents()
    RETURNS TRIGGER AS
$$
BEGIN
    IF new.parent_id != old.parent_id THEN
        RAISE DEBUG 'Updating tag_id % in tag_mapping_virtuals due to alias change to %', old.parent_id, new.parent_id;
        UPDATE tag_mappings_virtuals SET tag_id = COALESCE(new.parent_id, new.original_parent_id) WHERE tag_id = old.parent_id AND domain_id = old.domain_id;
    END IF;

    IF new.child_id != old.child_id THEN
        RAISE DEBUG 'Updating origin_id % in tag_mapping_virtuals due to alias change to %', old.child_id, new.child_id;
        UPDATE tag_mappings_virtuals SET origin_id = COALESCE(new.child_id, new.original_child_id) WHERE origin_id = old.child_id AND domain_id = old.domain_id;
    END IF;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION tag_virtuals_after_delete_aliased_parents()
    RETURNS TRIGGER AS
$$
BEGIN
    RAISE DEBUG 'Deleting tag_id % from tag_mapping_virtuals due to relationship with child % being deleted', COALESCE(old.parent_id, old.original_parent_id), COALESCE(old.child_id, old.original_child_id);
    DELETE FROM tag_mappings_virtuals WHERE tag_id = COALESCE(old.parent_id, old.original_parent_id) AND domain_id = old.domain_id;

    RETURN old;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tag_virtuals_aliased_parents_insert
    AFTER INSERT
    ON aliased_parents
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_after_insert_aliased_parents();

CREATE TRIGGER tag_virtuals_aliased_parents_update
    AFTER UPDATE
    ON aliased_parents
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_after_update_aliased_parents();

CREATE TRIGGER tag_virtuals_aliased_parents_delete
    AFTER DELETE
    ON aliased_parents
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_after_delete_aliased_parents();