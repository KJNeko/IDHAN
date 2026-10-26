CREATE FUNCTION collect_tag_count_storage() RETURNS TRIGGER AS
$$
BEGIN
    IF TG_OP = 'INSERT' THEN
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, storage_delta, display_delta)
        SELECT tag_id, tag_domain_id, SUM(storage_delta), SUM(display_delta)
        FROM (SELECT tag_id, tag_domain_id, 1 AS storage_delta, 0 AS display_delta
              FROM new_mappings
              UNION ALL
              SELECT COALESCE(ideal_tag_id, tag_id), tag_domain_id, 0, 1
              FROM new_mappings) changes
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id;
    ELSIF TG_OP = 'DELETE' THEN
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, storage_delta, display_delta)
        SELECT tag_id, tag_domain_id, SUM(storage_delta), SUM(display_delta)
        FROM (SELECT tag_id, tag_domain_id, -1 AS storage_delta, 0 AS display_delta
              FROM old_mappings
              UNION ALL
              SELECT COALESCE(ideal_tag_id, tag_id), tag_domain_id, 0, -1
              FROM old_mappings) changes
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id;
    ELSE
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, storage_delta, display_delta)
        SELECT tag_id, tag_domain_id, SUM(storage_delta), SUM(display_delta)
        FROM (SELECT tag_id, tag_domain_id, -1 AS storage_delta, 0 AS display_delta
              FROM old_mappings
              UNION ALL
              SELECT COALESCE(ideal_tag_id, tag_id), tag_domain_id, 0, -1
              FROM old_mappings
              UNION ALL
              SELECT tag_id, tag_domain_id, 1, 0
              FROM new_mappings
              UNION ALL
              SELECT COALESCE(ideal_tag_id, tag_id), tag_domain_id, 0, 1
              FROM new_mappings) changes
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id
        HAVING SUM(storage_delta) <> 0
            OR SUM(display_delta) <> 0;
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
