-- Normalize existing pairs to duplicate kings and discard pairs within the same group.
INSERT INTO alternative_records (lesser_record_id, greater_record_id)
SELECT DISTINCT LEAST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id)),
                GREATEST(duplicate_king(lesser_record_id), duplicate_king(greater_record_id))
FROM alternative_records
WHERE duplicate_king(lesser_record_id) <> duplicate_king(greater_record_id)
ON CONFLICT DO NOTHING;

DELETE
FROM alternative_records
WHERE lesser_record_id <> duplicate_king(lesser_record_id)
   OR greater_record_id <> duplicate_king(greater_record_id);

CREATE TRIGGER trg_normalize_alternative_pair
    BEFORE INSERT
    ON alternative_records
    FOR EACH ROW
EXECUTE FUNCTION normalize_ruled_pair();

CREATE TRIGGER trg_discard_alternative_distance
    AFTER INSERT
    ON alternative_records
    FOR EACH ROW
EXECUTE FUNCTION discard_ruled_distance();

CREATE TRIGGER trg_requeue_alternative_distance
    AFTER DELETE
    ON alternative_records
    FOR EACH ROW
EXECUTE FUNCTION requeue_ruled_distance();
