-- Delete legacy distances covered by rulings, using pair keys to avoid a full distance-table
-- scan.
DELETE
FROM hamming_distance hamming
    USING (SELECT LEAST(member.record_id, other.record_id)    AS left_id,
                  GREATEST(member.record_id, other.record_id) AS right_id
           FROM flattened_duplicates member
                    JOIN flattened_duplicates other USING (root_id)
           WHERE member.record_id < other.record_id) duplicate_pair
WHERE hamming.left_id = duplicate_pair.left_id
  AND hamming.right_id = duplicate_pair.right_id;

DELETE
FROM hamming_distance hamming
    USING (SELECT lesser_record_id, greater_record_id
           FROM alternative_records
           UNION ALL
           SELECT lesser_record_id, greater_record_id
           FROM unrelated_records) ruling,
    LATERAL duplicate_group_members(ruling.lesser_record_id) AS left_member(record_id),
    LATERAL duplicate_group_members(ruling.greater_record_id) AS right_member(record_id)
WHERE hamming.left_id = LEAST(left_member.record_id, right_member.record_id)
  AND hamming.right_id = GREATEST(left_member.record_id, right_member.record_id);
