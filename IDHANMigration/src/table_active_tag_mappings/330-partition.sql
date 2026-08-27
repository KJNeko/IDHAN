SET LOCAL work_mem = '256MB';

DROP VIEW active_tag_mappings_final;
ALTER TABLE active_tag_mappings
    RENAME TO active_tag_mappings_migration_330;
ALTER INDEX active_tag_mappings_pkey RENAME TO active_tag_mappings_migration_330_pkey;

CREATE TABLE active_tag_mappings
(
    record_id        INTEGER REFERENCES records (record_id)                            NOT NULL,
    tag_id           INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    ideal_tag_id     INTEGER REFERENCES tags (tag_id)                                  NULL,
    tag_domain_id    SMALLINT REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE NOT NULL,
    effective_tag_id INTEGER GENERATED ALWAYS AS (COALESCE(ideal_tag_id, tag_id)) VIRTUAL,
    PRIMARY KEY (record_id, tag_id, tag_domain_id),
    FOREIGN KEY (record_id, tag_id, tag_domain_id) REFERENCES tag_mappings (record_id, tag_id, tag_domain_id) ON DELETE CASCADE
) PARTITION BY LIST (tag_domain_id);

DO
$$
    DECLARE
        domain SMALLINT;
    BEGIN
        FOR domain IN SELECT tag_domain_id FROM tag_domains
            LOOP
                EXECUTE format(
                        'CREATE TABLE IF NOT EXISTS active_tag_mappings_domain_%s PARTITION OF active_tag_mappings FOR VALUES IN (%s)',
                        domain, domain);
            END LOOP;
    END
$$;

INSERT INTO active_tag_mappings (record_id, tag_id, ideal_tag_id, tag_domain_id)
SELECT record_id, tag_id, ideal_tag_id, tag_domain_id
FROM active_tag_mappings_migration_330
ORDER BY tag_domain_id, record_id, tag_id;

DROP TABLE active_tag_mappings_migration_330;

CREATE INDEX active_tag_mappings_tag_id_idx
    ON active_tag_mappings (tag_id) INCLUDE (record_id)
    WHERE ideal_tag_id IS NULL;

CREATE INDEX active_tag_mappings_ideal_tag_id_idx
    ON active_tag_mappings (ideal_tag_id) INCLUDE (record_id)
    WHERE ideal_tag_id IS NOT NULL;

ANALYZE active_tag_mappings;

CREATE TRIGGER trg_intercept_parent_mapping
    BEFORE INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION intercept_parent_mapping();

CREATE TRIGGER accumulate_tag_count_trigger
    AFTER INSERT OR DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION accumulate_tag_count_storage();

CREATE TRIGGER accumulate_tag_count_update_trigger
    AFTER UPDATE
    ON active_tag_mappings
    FOR EACH ROW
    WHEN (COALESCE(OLD.ideal_tag_id, OLD.tag_id) IS DISTINCT FROM COALESCE(NEW.ideal_tag_id, NEW.tag_id))
EXECUTE FUNCTION accumulate_tag_count_storage();

CREATE TRIGGER trg_insert_parents_from_active_mappings
    AFTER INSERT
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION insert_parents_from_active_mappings();

CREATE TRIGGER trg_delete_parents_from_active_mappings
    AFTER DELETE
    ON active_tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION delete_parents_from_active_mappings();

CREATE TRIGGER trg_repropagate_parents_on_ideal_change
    AFTER UPDATE
    ON active_tag_mappings
    FOR EACH ROW
    WHEN (COALESCE(OLD.ideal_tag_id, OLD.tag_id) IS DISTINCT FROM COALESCE(NEW.ideal_tag_id, NEW.tag_id))
EXECUTE FUNCTION repropagate_parents_on_ideal_change();

CREATE VIEW active_tag_mappings_final AS
(
SELECT record_id, tag_id, tag_domain_id
FROM active_tag_mappings
WHERE ideal_tag_id IS NULL
UNION ALL
SELECT record_id, ideal_tag_id AS tag_id, tag_domain_id
FROM active_tag_mappings
WHERE ideal_tag_id IS NOT NULL
UNION ALL
SELECT record_id, tag_id, tag_domain_id
FROM active_tag_mappings_parents);
