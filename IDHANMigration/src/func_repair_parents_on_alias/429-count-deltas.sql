CREATE OR REPLACE FUNCTION repair_parents_on_alias() RETURNS TRIGGER AS
$$
DECLARE
    previous_role   TEXT := current_setting('session_replication_role');
    changed_tags    INTEGER[];
    changed_domains SMALLINT[];
    changed_counts  BIGINT[];
BEGIN
    IF NOT EXISTS (SELECT 1
                   FROM new_aliases na
                            JOIN active_tag_mappings_parents p
                                 ON p.tag_id = na.aliased_id AND p.tag_domain_id = na.tag_domain_id) THEN
        RETURN NULL;
    END IF;

    -- Moving inherited rows must not recursively repropagate the same graph. Record
    -- the real deletions and insertions explicitly because this also bypasses counting.
    PERFORM set_config('session_replication_role', 'replica', true);
    WITH removed AS (
        DELETE FROM active_tag_mappings_parents p USING new_aliases na
            WHERE na.tag_domain_id = p.tag_domain_id AND na.aliased_id = p.tag_id
                AND COALESCE(na.ideal_alias_id, na.alias_id) IS DISTINCT FROM p.tag_id
            RETURNING p.record_id, p.tag_id AS old_tag_id,
                COALESCE(na.ideal_alias_id, na.alias_id) AS tag_id,
                p.origin_id, p.tag_domain_id, p.internal_count),
         moved AS (
             INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
                 SELECT record_id, tag_id, origin_id, tag_domain_id, MAX(internal_count)::INTEGER
                 FROM removed
                 GROUP BY record_id, tag_id, origin_id, tag_domain_id
                 ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id) DO UPDATE
                     SET internal_count = GREATEST(active_tag_mappings_parents.internal_count, EXCLUDED.internal_count)
                 RETURNING WITH (OLD AS previous) tag_id, tag_domain_id, previous.record_id IS NULL AS inserted),
         contributions AS (SELECT old_tag_id AS tag_id, tag_domain_id, -COUNT(*) AS delta
                           FROM removed
                           GROUP BY tag_domain_id, old_tag_id
                           UNION ALL
                           SELECT tag_id, tag_domain_id, COUNT(*)
                           FROM moved
                           WHERE inserted
                           GROUP BY tag_domain_id, tag_id),
         grouped AS (SELECT tag_id, tag_domain_id, SUM(delta)::BIGINT AS delta
                     FROM contributions
                     GROUP BY tag_domain_id, tag_id
                     HAVING SUM(delta) <> 0)
    SELECT array_agg(tag_id), array_agg(tag_domain_id), array_agg(delta)
    INTO changed_tags, changed_domains, changed_counts
    FROM grouped;

    PERFORM set_config('session_replication_role', previous_role, true);
    -- Restore the normal trigger mode before appending contributions.
    INSERT INTO tag_count_delta (tag_id, tag_domain_id, display_delta)
    SELECT tag_id, tag_domain_id, delta
    FROM unnest(changed_tags, changed_domains, changed_counts) AS changes(tag_id, tag_domain_id, delta)
             JOIN tag_domains USING (tag_domain_id);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
