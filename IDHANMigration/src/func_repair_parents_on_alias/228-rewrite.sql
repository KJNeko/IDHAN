CREATE OR REPLACE FUNCTION repair_parents_on_alias()
    RETURNS TRIGGER
AS
$$
BEGIN
    IF NOT EXISTS (SELECT 1
                   FROM new_aliases na
                            JOIN active_tag_mappings_parents p
                                 ON p.tag_id = na.aliased_id
                                     AND p.tag_domain_id = na.tag_domain_id) THEN
        RETURN NULL;
    END IF;

    PERFORM set_config('session_replication_role', 'replica', true);

    WITH to_fix AS (
        DELETE
            FROM active_tag_mappings_parents p
                USING new_aliases na
                WHERE na.tag_domain_id = p.tag_domain_id
                    AND na.aliased_id = p.tag_id
                    AND COALESCE(na.ideal_alias_id, na.alias_id) IS DISTINCT FROM p.tag_id
                RETURNING p.record_id,
                    COALESCE(na.ideal_alias_id, na.alias_id) AS tag_id,
                    p.origin_id,
                    p.tag_domain_id,
                    p.internal_count)
    INSERT
    INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id, internal_count)
    SELECT record_id,
           tag_id,
           origin_id,
           tag_domain_id,
           MAX(internal_count)::int AS internal_count
    FROM to_fix
    GROUP BY record_id, tag_id, origin_id, tag_domain_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count =
                          GREATEST(active_tag_mappings_parents.internal_count, excluded.internal_count);

    PERFORM set_config('session_replication_role', 'origin', true);

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;
