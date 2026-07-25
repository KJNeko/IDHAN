-- Drops the `LOCK TABLE tag_counts IN EXCLUSIVE MODE` from the original (036-create.sql). It was
-- redundant: tag_counts' primary key is exactly (tag_id, tag_domain_id), the same columns as the
-- ON CONFLICT target below, so INSERT ... ON CONFLICT DO UPDATE is already atomic and race-safe
-- per row without any explicit table lock. The EXCLUSIVE lock instead serialized every writer
-- touching tag_counts process-wide, once per row inserted into active_tag_mappings (via
-- accumulate_tag_count_trigger) and held until COMMIT — the dominant cost on bulk tag imports.
CREATE OR REPLACE FUNCTION add_count(tag_id_i INTEGER, ideal_tag_id_i INTEGER, tag_domain_id_i SMALLINT) RETURNS VOID AS
$$
BEGIN
    IF tag_id_i IS NOT NULL THEN
        INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count)
        VALUES (tag_id_i, tag_domain_id_i, 1)
        ON CONFLICT (tag_id, tag_domain_id)
            DO UPDATE SET storage_count = tag_counts.storage_count + 1;
    END IF;

    INSERT INTO tag_counts (tag_id, tag_domain_id, display_count)
    VALUES (COALESCE(ideal_tag_id_i, tag_id_i), tag_domain_id_i, 1)
    ON CONFLICT (tag_id, tag_domain_id)
        DO UPDATE SET display_count = tag_counts.display_count + 1;
END;
$$ LANGUAGE plpgsql;
