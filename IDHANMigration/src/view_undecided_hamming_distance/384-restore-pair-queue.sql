-- Exclude duplicate and alternative pairs; retain unrelated pairs. Resolve kings per record to
-- avoid joining the unindexed flattened_duplicates view.
CREATE OR REPLACE VIEW undecided_hamming_distance AS
SELECT hamming.left_id, hamming.right_id, hamming.distance
FROM hamming_distance hamming
WHERE NOT EXISTS (SELECT 1
                  FROM alternative_records alternative
                  WHERE alternative.lesser_record_id = hamming.left_id
                    AND alternative.greater_record_id = hamming.right_id)
  AND COALESCE((SELECT duplicate_group.king_id
                FROM duplicate_group_inferiors inferior
                         JOIN duplicate_groups duplicate_group USING (duplicate_id)
                WHERE inferior.inferior_id = hamming.left_id), hamming.left_id)
    <> COALESCE((SELECT duplicate_group.king_id
                 FROM duplicate_group_inferiors inferior
                          JOIN duplicate_groups duplicate_group USING (duplicate_id)
                 WHERE inferior.inferior_id = hamming.right_id), hamming.right_id);
