-- Keeps active_tag_mappings_parents.tag_id alias-resolved when a tag that is already materialised
-- as a parent is aliased afterwards.
--
-- Alias insertion already repairs the two *other* places a tag id is cached:
-- repair_tag_mappings_ideals_insert (028) fixes active_tag_mappings.ideal_tag_id, and
-- tag_aliases_after_insert_trigger (022) fixes tag_parents.ideal_parent_id / ideal_child_id.
-- Nothing touched the materialised parent rows, so they kept the now-aliased raw tag.
--
-- repropagate_parents_on_ideal_change (see its own directory) does not cover this: it fires on
-- active_tag_mappings UPDATE and handles only the *child* side -- it re-homes origin_id and
-- re-inserts parents for the new effective child. It never rewrites an existing atmp.tag_id.
--
-- This is why view_active_tag_mappings_final had to COALESCE its parents branch through a
-- LEFT JOIN against tag_aliases. That put the search predicate on the output of a join, so
-- `tag_id = <id>` could no longer be answered by active_tag_mappings_parents (tag_id) INCLUDE
-- (record_id) (043-index). Resolving at write time here lets 099-rewrite drop that COALESCE.
--
-- Statement-level, not row-level: aliases arrive in bulk, and the rewrite below suspends this
-- table's own triggers. Per statement that is one suspend/restore pair; per row it would be one
-- per alias.
--
-- session_replication_role rather than ALTER TABLE ... DISABLE TRIGGER: the latter takes an
-- ACCESS EXCLUSIVE lock held to COMMIT, which during a bulk alias import would block every other
-- reader and writer of active_tag_mappings_parents for the whole import -- the same class of
-- serialisation that func_add_count/094-rewrite and func_remove_count/095-rewrite removed.
-- set_config with is_local = true scopes the change to the current transaction, and it is
-- restored immediately after the two statements. Note this requires the connecting role to be
-- superuser, which is how the server connects.
--
-- The suspension is needed because both triggers on this table key off values the rewrite is
-- deliberately changing:
--   atmp_internal_on_delete decrements/removes rows WHERE origin_id = old.tag_id
--   atmp_internal_on_insert inserts parents for the newly inserted tag
-- Neither applies here. The row is not being retracted or newly granted; it is the same logical
-- parent being restored under its ideal id, so the propagation bookkeeping must stay as it is.
--
-- origin_id is intentionally the raw tag and is left alone.

CREATE OR REPLACE FUNCTION repair_parents_on_alias()
    RETURNS TRIGGER
AS
$$
BEGIN
    -- Cheap guard: most alias inserts do not name a tag that is currently materialised as a
    -- parent, and those must not pay for the suspend/restore below.
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
           SUM(internal_count)::int AS internal_count
    FROM to_fix
    GROUP BY record_id, tag_id, origin_id, tag_domain_id
    ON CONFLICT (record_id, tag_id, origin_id, tag_domain_id)
        DO UPDATE SET internal_count = active_tag_mappings_parents.internal_count + excluded.internal_count;

    PERFORM set_config('session_replication_role', 'origin', true);

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER repair_parents_on_alias_insert
    AFTER INSERT
    ON tag_aliases
    REFERENCING NEW TABLE AS new_aliases
    FOR EACH STATEMENT
EXECUTE FUNCTION repair_parents_on_alias();

CREATE TRIGGER repair_parents_on_alias_update
    AFTER UPDATE
    ON tag_aliases
    REFERENCING NEW TABLE AS new_aliases
    FOR EACH STATEMENT
EXECUTE FUNCTION repair_parents_on_alias();
