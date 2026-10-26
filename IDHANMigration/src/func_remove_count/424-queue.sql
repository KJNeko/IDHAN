CREATE OR REPLACE FUNCTION remove_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT)
    RETURNS VOID AS
$$
BEGIN
    INSERT INTO tag_count_delta (tag_id, tag_domain_id, storage_delta, display_delta)
    SELECT tag_id, tag_domain_id_i, SUM(storage_delta), SUM(display_delta)
    FROM (VALUES (tag_id_i, -1, 0), (COALESCE(ideal_tag_id_i, tag_id_i), 0, -1))
             AS changes(tag_id, storage_delta, display_delta)
    WHERE tag_id IS NOT NULL
      AND EXISTS (SELECT 1 FROM tag_domains WHERE tag_domain_id = tag_domain_id_i)
    GROUP BY tag_id;
END;
$$ LANGUAGE plpgsql;
