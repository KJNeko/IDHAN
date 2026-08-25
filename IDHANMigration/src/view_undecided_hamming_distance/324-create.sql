-- Pairs close enough to be worth comparing that no one has ruled on yet. A pair drops out when it
-- is an alternative, or when both sides flatten onto the same duplicate. `unrelated_records` is
-- deliberately not consulted, so dismissed lookalikes still surface.
CREATE OR REPLACE VIEW undecided_hamming_distance AS
SELECT hamming.left_id, hamming.right_id, hamming.distance
FROM hamming_distance hamming
         LEFT JOIN flattened_duplicates left_side ON left_side.record_id = hamming.left_id
         LEFT JOIN flattened_duplicates right_side ON right_side.record_id = hamming.right_id
WHERE NOT EXISTS (SELECT 1
                  FROM alternative_records alternative
                  WHERE alternative.lesser_record_id = hamming.left_id
                    AND alternative.greater_record_id = hamming.right_id)
  AND (left_side.root_id IS NULL OR right_side.root_id IS NULL OR left_side.root_id != right_side.root_id);
