-- Project both records onto their duplicate kings; ungrouped records resolve to themselves.
CREATE OR REPLACE VIEW alternative_kings AS
SELECT DISTINCT LEAST(COALESCE(lesser_side.root_id, alternative.lesser_record_id),
                      COALESCE(greater_side.root_id, alternative.greater_record_id))    AS lesser_king_id,
                GREATEST(COALESCE(lesser_side.root_id, alternative.lesser_record_id),
                         COALESCE(greater_side.root_id, alternative.greater_record_id)) AS greater_king_id
FROM alternative_records alternative
         LEFT JOIN flattened_duplicates lesser_side ON lesser_side.record_id = alternative.lesser_record_id
         LEFT JOIN flattened_duplicates greater_side ON greater_side.record_id = alternative.greater_record_id;
