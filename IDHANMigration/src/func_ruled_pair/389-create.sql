-- Normalize alternative and unrelated pairs to duplicate kings. Discard pairs within the same
-- duplicate group.
CREATE OR REPLACE FUNCTION normalize_ruled_pair()
    RETURNS trigger
AS
$$
DECLARE
    lesser_king  INTEGER := duplicate_king(new.lesser_record_id);
    greater_king INTEGER := duplicate_king(new.greater_record_id);
BEGIN
    IF lesser_king = greater_king THEN
        RETURN NULL;
    END IF;

    new.lesser_record_id := LEAST(lesser_king, greater_king);
    new.greater_record_id := GREATEST(lesser_king, greater_king);

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Delete distances between all members of the ruled groups.
CREATE OR REPLACE FUNCTION discard_ruled_distance()
    RETURNS trigger
AS
$$
BEGIN
    DELETE
    FROM hamming_distance hamming
        USING duplicate_group_members(new.lesser_record_id) AS left_member(record_id),
        duplicate_group_members(new.greater_record_id) AS right_member(record_id)
    WHERE hamming.left_id = LEAST(left_member.record_id, right_member.record_id)
      AND hamming.right_id = GREATEST(left_member.record_id, right_member.record_id);

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- Requeue kings when a ruling is cleared. Skip requeueing when normalization moved the ruling
-- to another king.
CREATE OR REPLACE FUNCTION requeue_ruled_distance()
    RETURNS trigger
AS
$$
BEGIN
    IF pair_is_ruled(old.lesser_record_id, old.greater_record_id) THEN
        RETURN NULL;
    END IF;

    INSERT INTO hamming_distance_queue (record_id)
    VALUES (old.lesser_record_id),
           (old.greater_record_id)
    ON CONFLICT (record_id) DO NOTHING;

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
