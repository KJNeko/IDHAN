CREATE OR REPLACE FUNCTION pair_is_ruled(record_a INTEGER, record_b INTEGER) RETURNS BOOLEAN AS
$$
WITH roots AS MATERIALIZED (SELECT duplicate_king(record_a) AS a, duplicate_king(record_b) AS b)
SELECT roots.a = roots.b
           OR EXISTS (SELECT 1
                      FROM alternative_records alternative
                      WHERE alternative.lesser_record_id = LEAST(roots.a, roots.b)
                        AND alternative.greater_record_id = GREATEST(roots.a, roots.b))
           OR EXISTS (SELECT 1
                      FROM unrelated_records unrelated
                      WHERE unrelated.lesser_record_id = LEAST(roots.a, roots.b)
                        AND unrelated.greater_record_id = GREATEST(roots.a, roots.b))
FROM roots;
$$ LANGUAGE sql STABLE;
