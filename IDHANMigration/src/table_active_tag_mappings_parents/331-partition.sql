SET LOCAL work_mem = '256MB';

DROP VIEW active_tag_mappings_final;
ALTER TABLE active_tag_mappings_parents
    RENAME TO active_tag_mappings_parents_migration_331;
ALTER INDEX active_tag_mappings_parents_pkey RENAME TO active_tag_mappings_parents_migration_331_pkey;

CREATE TABLE active_tag_mappings_parents
(
    record_id      INTEGER REFERENCES records (record_id)                            NOT NULL,
    tag_id         INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    origin_id      INTEGER REFERENCES tags (tag_id)                                  NOT NULL,
    internal_count INTEGER DEFAULT 0                                                 NOT NULL,
    tag_domain_id  SMALLINT REFERENCES tag_domains (tag_domain_id) ON DELETE CASCADE NOT NULL,
    internal       BOOLEAN GENERATED ALWAYS AS ( internal_count > 0 ) VIRTUAL,
    PRIMARY KEY (record_id, tag_id, origin_id, tag_domain_id)
) PARTITION BY LIST (tag_domain_id);

DO
$$
    DECLARE
        domain SMALLINT;
    BEGIN
        FOR domain IN SELECT tag_domain_id FROM tag_domains
            LOOP
                EXECUTE format(
                        'CREATE TABLE IF NOT EXISTS active_tag_mappings_parents_domain_%s PARTITION OF active_tag_mappings_parents FOR VALUES IN (%s)',
                        domain, domain);
            END LOOP;
    END
$$;

INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, internal_count, tag_domain_id)
SELECT record_id, tag_id, origin_id, internal_count, tag_domain_id
FROM active_tag_mappings_parents_migration_331
ORDER BY tag_domain_id, record_id, tag_id, origin_id;

DROP TABLE active_tag_mappings_parents_migration_331;

CREATE INDEX active_tag_mappings_parents_tag_id_idx
    ON active_tag_mappings_parents (tag_id) INCLUDE (record_id);

ANALYZE active_tag_mappings_parents;

CREATE TRIGGER trg_accumulate_tag_count_parents
    AFTER INSERT OR DELETE
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION accumulate_tag_count_parents();

CREATE TRIGGER trg_atmp_internal_insert
    AFTER INSERT
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_insert();

CREATE TRIGGER trg_atmp_internal_delete
    AFTER DELETE
    ON active_tag_mappings_parents
    FOR EACH ROW
EXECUTE FUNCTION atmp_internal_on_delete();

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
