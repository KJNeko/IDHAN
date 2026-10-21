-- Project unrelated pairs onto duplicate kings.
CREATE OR REPLACE VIEW unrelated_kings AS
SELECT DISTINCT LEAST(COALESCE(lesser_side.root_id, unrelated.lesser_record_id),
                      COALESCE(greater_side.root_id, unrelated.greater_record_id))    AS lesser_king_id,
                GREATEST(COALESCE(lesser_side.root_id, unrelated.lesser_record_id),
                         COALESCE(greater_side.root_id, unrelated.greater_record_id)) AS greater_king_id
FROM unrelated_records unrelated
         LEFT JOIN flattened_duplicates lesser_side ON lesser_side.record_id = unrelated.lesser_record_id
         LEFT JOIN flattened_duplicates greater_side ON greater_side.record_id = unrelated.greater_record_id;
