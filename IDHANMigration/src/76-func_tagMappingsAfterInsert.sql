CREATE OR REPLACE FUNCTION func_tag_mappings_after_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings(record_id, tag_id, tag_domain_id, ideal_tag_id)
    SELECT new_rows.record_id,
           new_rows.tag_id,
           new_rows.tag_domain_id,
           COALESCE(ta.ideal_alias_id, ta.alias_id, NULL)
    FROM new_rows
             JOIN file_info fi ON fi.record_id = new_rows.record_id
             LEFT JOIN tag_aliases ta ON ta.aliased_id = new_rows.tag_id
    ON CONFLICT (record_id, tag_id, tag_domain_id)
        DO NOTHING;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create trigger to execute the function after insert on tag_mappings
CREATE TRIGGER trg_tag_mappings_after_insert
    AFTER INSERT
    ON tag_mappings
    REFERENCING new TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_tag_mappings_after_insert();

CREATE FUNCTION func_tag_mappings_after_active_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    -- new_rows is the table NEW (file_info)

    INSERT INTO active_tag_mappings (record_id, tag_id, tag_domain_id, ideal_tag_id)
    SELECT new_rows.record_id, tag_id, tm.tag_domain_id, COALESCE(ta.ideal_alias_id, ta.alias_id, NULL) AS ideal_tag_id
    FROM new_rows
             JOIN tag_mappings tm ON tm.record_id = new_rows.record_id
             LEFT JOIN tag_aliases ta ON ta.aliased_id = tm.tag_id
    ON CONFLICT (record_id, tag_id, tag_domain_id) DO NOTHING;

    RETURN new;
END;
$$ LANGUAGE plpgsql;


CREATE TRIGGER trg_tag_mappings_after_active_insert
    AFTER INSERT
    ON file_info
    REFERENCING new TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_tag_mappings_after_active_insert();