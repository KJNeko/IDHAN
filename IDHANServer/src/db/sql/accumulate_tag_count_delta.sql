-- Consume a complete committed snapshot, rather than an ID-ordered subset that
-- could apply a removal before its corresponding addition. Never truncate: new
-- appends and rolled-back deletions must remain available to the next pass.
WITH pending AS (
    DELETE FROM tag_count_delta
        RETURNING tag_id, tag_domain_id, storage_delta, display_delta),
     applied AS (
         INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count)
             SELECT tag_id, tag_domain_id, SUM(storage_delta)::INTEGER, SUM(display_delta)::INTEGER
             FROM pending
             GROUP BY tag_domain_id, tag_id
             HAVING SUM(storage_delta) <> 0
                 OR SUM(display_delta) <> 0
             ORDER BY tag_domain_id, tag_id
             ON CONFLICT (tag_id, tag_domain_id) DO UPDATE
                 SET storage_count = tag_counts.storage_count + EXCLUDED.storage_count,
                     display_count = tag_counts.display_count + EXCLUDED.display_count
             RETURNING tag_id)
SELECT (SELECT COUNT(*) FROM pending), (SELECT COUNT(*) FROM applied);
