-- Normalize existing unrelated pairs to duplicate kings.
INSERT INTO unrelated_records (lesser_record_id, greater_record_id)
SELECT DISTINCT LEAST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id)),
                GREATEST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id))
FROM unrelated_records
WHERE duplicate_king(lesser_record_id) <> duplicate_king(greater_record_id)
ON CONFLICT DO NOTHING;

DELETE
FROM unrelated_records
WHERE lesser_record_id <> duplicate_king(lesser_record_id)
   OR greater_record_id <> duplicate_king(greater_record_id);

CREATE TRIGGER trg_normalize_unrelated_pair
    BEFORE INSERT
    ON unrelated_records
    FOR EACH ROW
EXECUTE FUNCTION normalize_ruled_pair();

CREATE TRIGGER trg_discard_unrelated_distance
    AFTER INSERT
    ON unrelated_records
    FOR EACH ROW
EXECUTE FUNCTION discard_ruled_distance();

CREATE TRIGGER trg_requeue_unrelated_distance
    AFTER DELETE
    ON unrelated_records
    FOR EACH ROW
EXECUTE FUNCTION requeue_ruled_distance();
