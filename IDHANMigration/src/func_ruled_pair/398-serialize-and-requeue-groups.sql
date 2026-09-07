-- Acquire the same lock as duplicate changes and the sweeper before row locks or normalization.
CREATE OR REPLACE FUNCTION lock_relationship_changes() RETURNS trigger AS
$$
BEGIN
    PERFORM pg_advisory_xact_lock(16609, 1);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_lock_alternative_changes
    BEFORE INSERT OR UPDATE OR DELETE ON alternative_records
    FOR EACH STATEMENT EXECUTE FUNCTION lock_relationship_changes();
CREATE TRIGGER trg_lock_unrelated_changes
    BEFORE INSERT OR UPDATE OR DELETE ON unrelated_records
    FOR EACH STATEMENT EXECUTE FUNCTION lock_relationship_changes();

-- Requeue every group member to restore inferior-to-inferior distances, even when the kings
-- have dissimilar hashes.
CREATE OR REPLACE FUNCTION requeue_ruled_distance() RETURNS trigger AS
$$
BEGIN
    IF pair_is_ruled(old.lesser_record_id, old.greater_record_id) THEN
        RETURN NULL;
    END IF;

    INSERT INTO hamming_distance_queue (record_id)
    SELECT duplicate_group_members(duplicate_king(old.lesser_record_id))
    UNION
    SELECT duplicate_group_members(duplicate_king(old.greater_record_id))
    ON CONFLICT (record_id) DO NOTHING;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
