-- Every file_info update rebuilt the per-cluster delta aggregate, including the ones that only touch
-- modified_time or mime_id. A column list on the trigger would be the cheaper guard, but postgres
-- rejects one on a trigger with transition tables.
CREATE OR REPLACE FUNCTION func_file_info_cluster_usage()
    RETURNS TRIGGER AS
$$
BEGIN
    IF tg_op = 'INSERT' THEN
        UPDATE file_clusters fc
        SET size_used  = fc.size_used + agg.total_size,
            file_count = fc.file_count + agg.total_count
        FROM (SELECT cluster_id, SUM(size) AS total_size, COUNT(*) AS total_count
              FROM new_rows
              WHERE cluster_id IS NOT NULL
              GROUP BY cluster_id) agg
        WHERE fc.cluster_id = agg.cluster_id;

    ELSIF tg_op = 'DELETE' THEN
        UPDATE file_clusters fc
        SET size_used  = fc.size_used - agg.total_size,
            file_count = fc.file_count - agg.total_count
        FROM (SELECT cluster_id, SUM(size) AS total_size, COUNT(*) AS total_count
              FROM old_rows
              WHERE cluster_id IS NOT NULL
              GROUP BY cluster_id) agg
        WHERE fc.cluster_id = agg.cluster_id;

    ELSE
        IF NOT EXISTS (SELECT 1
                       FROM old_rows o
                                JOIN new_rows n USING (record_id)
                       WHERE o.size IS DISTINCT FROM n.size
                          OR o.cluster_id IS DISTINCT FROM n.cluster_id) THEN
            RETURN NULL;
        END IF;

        UPDATE file_clusters fc
        SET size_used  = fc.size_used + d.size_delta,
            file_count = fc.file_count + d.count_delta
        FROM (SELECT cluster_id, SUM(size_delta) AS size_delta, SUM(count_delta) AS count_delta
              FROM (SELECT cluster_id, -size AS size_delta, -1 AS count_delta
                    FROM old_rows
                    WHERE cluster_id IS NOT NULL
                    UNION ALL
                    SELECT cluster_id, size, 1
                    FROM new_rows
                    WHERE cluster_id IS NOT NULL) changes
              GROUP BY cluster_id) d
        WHERE fc.cluster_id = d.cluster_id
          AND (d.size_delta <> 0 OR d.count_delta <> 0);
    END IF;

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
