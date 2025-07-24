ALTER TABLE tag_mappings_virtual
    ADD COLUMN self_origin BOOLEAN DEFAULT FALSE;

CREATE FUNCTION after_insert_delete_on_tag_mappings_virtual()
    RETURNS TRIGGER AS
$$
BEGIN

    CASE
        WHEN tg_op = 'INSERT' THEN -- Insert any parents that are chained, set self_origin to true
        RAISE DEBUG 'tag_virtuals_self: Inserted mapping for record % with origin % for mapping % to tag_mappings_virtual_%: Self reference: %', new.record_id, new.origin_id, new.tag_id, new.domain_id, new.self_origin;

        INSERT INTO tag_mappings_virtual (record_id, origin_id, tag_id, domain_id, self_origin)
        SELECT new.record_id, new.tag_id AS origin_id, COALESCE(ap.parent_id, ap.original_parent_id) AS tag_id, new.domain_id, TRUE
        FROM aliased_parents ap
        WHERE COALESCE(ap.child_id, ap.original_child_id) = new.tag_id
          AND ap.domain_id = new.domain_id
        ON CONFLICT DO NOTHING;

        -- Really shouldn't be triggered, I would think
        -- WHEN tg_op = 'UPDATE' THEN RAISE DEBUG 'tag_virtuals_self: Updated mapping for record % with origin %->% for mapping %->% to tag_mappings_virtual_%', new.record_id, old.origin_id, new.origin_id, old.tag_id, new.tag_id, new.domain_id;

        WHEN tg_op = 'DELETE' THEN -- We should only delete a tag we've inserted, So it will be a self-origin and with a matching domain, it's origin tag should be the tag inserted
        RAISE DEBUG 'tag_virtuals_self: Deleted self origin mapping for record % with origin % for mapping % to tag_mappings_virtual_%', old.record_id, old.origin_id, old.tag_id, old.domain_id;
        DELETE FROM tag_mappings_virtual WHERE self_origin = TRUE AND domain_id = old.domain_id AND record_id = old.record_id AND origin_id = old.tag_id;
        END CASE;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER a_id_tag_mappings_virtual
    AFTER INSERT OR DELETE
    ON tag_mappings_virtual
    FOR EACH ROW
EXECUTE FUNCTION after_insert_delete_on_tag_mappings_virtual();

