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

    PERFORM process_record_sibling_silencing(sub.record_id, sub.tag_domain_id)
    FROM (SELECT DISTINCT record_id, tag_domain_id FROM new_rows) sub;

    RETURN new;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION func_tag_mappings_after_active_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings (record_id, tag_id, tag_domain_id, ideal_tag_id)
    SELECT new_rows.record_id, tag_id, tm.tag_domain_id, COALESCE(ta.ideal_alias_id, ta.alias_id) AS ideal_tag_id
    FROM new_rows
             JOIN tag_mappings tm ON tm.record_id = new_rows.record_id
             LEFT JOIN tag_aliases ta ON ta.aliased_id = tm.tag_id
    ON CONFLICT (record_id, tag_id, tag_domain_id) DO NOTHING;

    PERFORM process_record_sibling_silencing(sub.record_id, sub.tag_domain_id)
    FROM (SELECT DISTINCT tm.record_id, tm.tag_domain_id
          FROM new_rows
                   JOIN tag_mappings tm ON tm.record_id = new_rows.record_id) sub;

    RETURN new;
END;
$$ LANGUAGE plpgsql;
