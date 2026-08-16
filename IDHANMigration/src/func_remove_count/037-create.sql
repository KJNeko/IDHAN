CREATE FUNCTION remove_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN

    LOCK TABLE tag_counts IN EXCLUSIVE MODE;

    UPDATE tag_counts SET storage_count = storage_count - 1 WHERE tag_id = tag_id_i AND tag_domain_id = tag_domain_id_i;
    UPDATE tag_counts
    SET display_count = display_count - 1
    WHERE tag_id = COALESCE(ideal_tag_id_i, tag_id_i)
      AND tag_domain_id = tag_domain_id_i;
END;
$$ LANGUAGE plpgsql;
