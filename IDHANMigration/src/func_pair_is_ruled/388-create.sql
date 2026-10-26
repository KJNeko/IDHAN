-- Resolve rulings through duplicate kings so each ruling applies to all members of both groups.
CREATE OR REPLACE FUNCTION pair_is_ruled(
    record_a INTEGER,
    record_b INTEGER
) RETURNS BOOLEAN AS
$$
SELECT duplicate_king(record_a) = duplicate_king(record_b)
    OR EXISTS (SELECT 1
               FROM alternative_records alternative
               WHERE alternative.lesser_record_id = LEAST(duplicate_king(record_a), duplicate_king(record_b))
                 AND alternative.greater_record_id = GREATEST(duplicate_king(record_a), duplicate_king(record_b)))
    OR EXISTS (SELECT 1
               FROM unrelated_records unrelated
               WHERE unrelated.lesser_record_id = LEAST(duplicate_king(record_a), duplicate_king(record_b))
                 AND unrelated.greater_record_id = GREATEST(duplicate_king(record_a), duplicate_king(record_b)));
$$ LANGUAGE sql STABLE;
