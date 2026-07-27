-- Drops the `LOCK TABLE tag_counts IN EXCLUSIVE MODE` from the original (037-create.sql), for the
-- same reason as func_add_count/094-rewrite.sql: both UPDATEs below match on tag_counts' primary
-- key (tag_id, tag_domain_id), so each hits at most one row and relies on ordinary row-level
-- locking — no table-level lock is needed for correctness, and the EXCLUSIVE lock was serializing
-- every concurrent writer to tag_counts process-wide.
CREATE OR REPLACE FUNCTION remove_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN
    UPDATE tag_counts SET storage_count = storage_count - 1 WHERE tag_id = tag_id_i AND tag_domain_id = tag_domain_id_i;
    UPDATE tag_counts
    SET display_count = display_count - 1
    WHERE tag_id = COALESCE(ideal_tag_id_i, tag_id_i)
      AND tag_domain_id = tag_domain_id_i;
END;
$$ LANGUAGE plpgsql;
