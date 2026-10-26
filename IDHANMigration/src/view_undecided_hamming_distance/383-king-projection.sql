-- Project pairs onto duplicate kings and retain the minimum distance per king pair. Exclude
-- pairs within one group or between alternative groups.
CREATE OR REPLACE VIEW undecided_hamming_distance AS
SELECT projected.left_id, projected.right_id, MIN(projected.distance) AS distance
FROM (SELECT LEAST(COALESCE(left_side.root_id, hamming.left_id),
                   COALESCE(right_side.root_id, hamming.right_id))    AS left_id,
             GREATEST(COALESCE(left_side.root_id, hamming.left_id),
                      COALESCE(right_side.root_id, hamming.right_id)) AS right_id,
             hamming.distance
      FROM hamming_distance hamming
               LEFT JOIN flattened_duplicates left_side ON left_side.record_id = hamming.left_id
               LEFT JOIN flattened_duplicates right_side ON right_side.record_id = hamming.right_id) projected
WHERE projected.left_id != projected.right_id
  AND NOT EXISTS (SELECT 1
                  FROM alternative_kings alternative
                  WHERE alternative.lesser_king_id = projected.left_id
                    AND alternative.greater_king_id = projected.right_id)
GROUP BY projected.left_id, projected.right_id;
