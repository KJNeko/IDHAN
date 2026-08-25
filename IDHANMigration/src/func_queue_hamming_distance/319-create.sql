CREATE OR REPLACE FUNCTION queue_hamming_distance()
    RETURNS trigger
AS
$$
BEGIN

    INSERT INTO hamming_distance_queue (record_id)
    VALUES (new.record_id)
    ON CONFLICT (record_id) DO NOTHING;

    RETURN new;
END;
$$ LANGUAGE plpgsql VOLATILE;

CREATE TRIGGER trg_queue_hamming_distance_insert
    AFTER INSERT
    ON image_metadata
    FOR EACH ROW
    WHEN (new.phash IS NOT NULL)
EXECUTE FUNCTION queue_hamming_distance();

CREATE TRIGGER trg_queue_hamming_distance_update
    AFTER UPDATE OF phash
    ON image_metadata
    FOR EACH ROW
    WHEN (old.phash IS NULL AND new.phash IS NOT NULL)
EXECUTE FUNCTION queue_hamming_distance();
