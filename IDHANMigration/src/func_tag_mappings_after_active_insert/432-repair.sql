-- Migration 370 replaced file_info and lost its tag-activation trigger. Rebuild all
-- derived mappings first, including domains whose active tables were truncated.
-- Raw mappings, aliases, parents, tags, and file records remain the source of truth.
SET LOCAL work_mem = '256MB';

-- Exclude the count worker before taking table locks, using its normal queue lock.
SELECT pg_advisory_xact_lock('tag_count_delta'::regclass::oid::bigint);
LOCK TABLE tag_domains, file_info, tag_mappings, tag_aliases, tag_parents
    IN SHARE ROW EXCLUSIVE MODE;

TRUNCATE active_tag_mappings, active_tag_mappings_parents, tag_counts, tag_count_delta;

-- The existing intercept/parent triggers resolve domain-local aliases and rebuild
-- inherited parent rows and their reference counts. Do not copy stale derived rows.
INSERT INTO active_tag_mappings (record_id, tag_id, tag_domain_id)
SELECT tm.record_id, tm.tag_id, tm.tag_domain_id
FROM tag_mappings tm
         JOIN file_info fi ON fi.record_id = tm.record_id
ORDER BY tm.tag_domain_id, tm.record_id, tm.tag_id;

-- TRUNCATE does not emit deletion deltas. Recompute the count baseline instead of
-- applying insert deltas on top of potentially stale counts from before the damage.
WITH storage AS (SELECT tag_id, tag_domain_id, COUNT(*) AS total
                 FROM active_tag_mappings
                 GROUP BY tag_id, tag_domain_id),
     display AS (SELECT tag_id, tag_domain_id, COUNT(*) AS total
                 FROM active_tag_mappings_final
                 GROUP BY tag_id, tag_domain_id)
INSERT
INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count)
SELECT tag_id, tag_domain_id, COALESCE(s.total, 0), COALESCE(d.total, 0)
FROM storage s
         FULL JOIN display d USING (tag_id, tag_domain_id);

-- The rebuilt baseline already includes the contributions queued by the rebuild.
TRUNCATE tag_count_delta;

-- Both arrival orders use active_tag_mappings' BEFORE INSERT trigger for alias
-- resolution, avoiding obsolete alias columns and joins across unrelated domains.
CREATE OR REPLACE FUNCTION func_tag_mappings_after_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings (record_id, tag_id, tag_domain_id)
    SELECT nm.record_id, nm.tag_id, nm.tag_domain_id
    FROM new_rows nm
             JOIN file_info fi ON fi.record_id = nm.record_id
    ON CONFLICT (record_id, tag_id, tag_domain_id) DO NOTHING;

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION func_tag_mappings_after_active_insert()
    RETURNS TRIGGER AS
$$
BEGIN
    INSERT INTO active_tag_mappings (record_id, tag_id, tag_domain_id)
    SELECT tm.record_id, tm.tag_id, tm.tag_domain_id
    FROM new_rows nf
             JOIN tag_mappings tm ON tm.record_id = nf.record_id
    ON CONFLICT (record_id, tag_id, tag_domain_id) DO NOTHING;

    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_tag_mappings_after_insert ON tag_mappings;
CREATE TRIGGER trg_tag_mappings_after_insert
    AFTER INSERT
    ON tag_mappings
    REFERENCING NEW TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_tag_mappings_after_insert();

DROP TRIGGER IF EXISTS trg_tag_mappings_after_active_insert ON file_info;
CREATE TRIGGER trg_tag_mappings_after_active_insert
    AFTER INSERT
    ON file_info
    REFERENCING NEW TABLE AS new_rows
    FOR EACH STATEMENT
EXECUTE FUNCTION func_tag_mappings_after_active_insert();

ANALYZE active_tag_mappings;
ANALYZE active_tag_mappings_parents;
ANALYZE tag_counts;
