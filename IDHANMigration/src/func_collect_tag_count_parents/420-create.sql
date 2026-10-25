CREATE FUNCTION collect_tag_count_parents() RETURNS TRIGGER AS
$$
BEGIN
    IF TG_OP = 'INSERT' THEN
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, display_delta)
        SELECT tag_id, tag_domain_id, COUNT(*)
        FROM new_mappings
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id;
    ELSIF TG_OP = 'DELETE' THEN
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, display_delta)
        SELECT tag_id, tag_domain_id, -COUNT(*)
        FROM old_mappings
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id;
    ELSE
        -- Refcount-only updates do not change display contributions. Key changes do.
        INSERT INTO tag_count_delta (tag_id, tag_domain_id, display_delta)
        SELECT tag_id, tag_domain_id, SUM(delta)
        FROM (SELECT tag_id, tag_domain_id, -1 AS delta
              FROM old_mappings
              UNION ALL
              SELECT tag_id, tag_domain_id, 1
              FROM new_mappings) changes
                 JOIN tag_domains USING (tag_domain_id)
        GROUP BY tag_domain_id, tag_id
        HAVING SUM(delta) <> 0;
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
