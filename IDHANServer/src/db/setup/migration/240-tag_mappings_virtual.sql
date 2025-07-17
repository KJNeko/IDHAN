CREATE TABLE tag_mappings_virtuals
(
    record_id INTEGER  NOT NULL REFERENCES records (record_id),
    origin_id INTEGER  NOT NULL REFERENCES tags (tag_id),
    tag_id    INTEGER  NOT NULL REFERENCES tags (tag_id),
    domain_id SMALLINT NOT NULL REFERENCES tag_domains (tag_domain_id),
    UNIQUE (record_id, tag_id)
);

-- Trigger for new inserts into tag_mappings
CREATE FUNCTION tag_virtuals_mappings_trigger()
    RETURNS TRIGGER AS
$$
BEGIN
    CASE
        WHEN tg_op = 'UPDATE' THEN -- Updates will happen when the ideal tag is updated.

        IF old.ideal_tag_id = new.ideal_tag_id THEN
            -- Ideal was not changed, This is abnormal
            RAISE EXCEPTION 'tag_mapping should only be updated through the ideal_tag_id, Which it was not';
        END IF;

        -- This means the ideal id got changed
        IF new.ideal_tag_id IS NULL THEN
            -- The new ideal is null, Meaning the chain has been wiped. So we should update anything with the origin_id to return back to the origin_id
            UPDATE tag_mappings_virtuals SET tag_id = origin_id WHERE tag_id = old.ideal_tag_id AND record_id = old.record_id;
        ELSE
            RAISE DEBUG 'Updating tag_id for % to %', old.ideal_tag_id, new.ideal_tag_id;
            UPDATE tag_mappings_virtuals SET tag_id = new.ideal_tag_id WHERE tag_id = old.ideal_tag_id AND record_id = old.record_id;
        END IF;

        WHEN tg_op = 'INSERT' THEN -- Insert all parent relationships into tag_mappings_virtuals
        INSERT INTO tag_mappings_virtuals (record_id, tag_id, origin_id, domain_id)
        SELECT DISTINCT new.record_id, ap.parent_id AS tag_id, COALESCE(new.ideal_tag_id, new.tag_id) AS origin_id, new.domain_id
        FROM aliased_parents ap
        WHERE ap.child_id = new.tag_id
          AND ap.domain_id = new.domain_id
        ON CONFLICT DO NOTHING;

        WHEN tg_op = 'DELETE' THEN -- Delete any relationships
        DELETE FROM tag_mappings_virtuals WHERE record_id = old.record_id AND tag_id = old.tag_id;
        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create the trigger
CREATE TRIGGER tag_virtuals_after_mappings
    AFTER INSERT OR UPDATE OR DELETE
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_mappings_trigger();


CREATE FUNCTION tag_virtuals_parents_trigger()
    RETURNS TRIGGER AS
$$
BEGIN
    CASE
        WHEN tg_op = 'INSERT' THEN -- Find any mappings that would use this and insert them
        INSERT INTO tag_mappings_virtuals (record_id, tag_id, origin_id, domain_id)
        SELECT DISTINCT record_id, new.parent_id AS tag_id, tag_id AS origin_id, domain_id
        FROM tag_mappings
        WHERE COALESCE(ideal_tag_id, tag_id) = new.child_id
          AND domain_id = new.domain_id;
        WHEN tg_op = 'UPDATE' THEN -- fix any now changed aliased tags
        RAISE DEBUG 'Updating virtual parents tag_id for % to %', old.parent_id, new.parent_id;
        UPDATE tag_mappings_virtuals SET tag_id = new.parent_id WHERE tag_id = old.parent_id AND domain_id = old.domain_id;
        UPDATE tag_mappings_virtuals SET origin_id = new.child_id WHERE origin_id = old.child_id AND domain_id = old.domain_id;
        WHEN tg_op = 'DELETE' THEN DELETE FROM tag_mappings_virtuals WHERE record_id = old.record_id AND tag_id = old.parent_id AND domain_id = old.domain_id;
        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER tag_virtuals_after_parents
    AFTER INSERT OR UPDATE OR DELETE
    ON aliased_parents
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_parents_trigger();