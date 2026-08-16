CREATE OR REPLACE FUNCTION func_tag_mappings_after_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings(record_id, tag_id, tag_domain_id, ideal_tag_id)
    SELECT new_rows.record_id,
           new_rows.tag_id,
           new_rows.tag_domain_id,
           ta.effective_tag_id
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
