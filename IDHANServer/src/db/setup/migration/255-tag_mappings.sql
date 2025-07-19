CREATE OR REPLACE FUNCTION tag_mappings_reorder_v1(p_tag_domain_id INTEGER)
    RETURNS VOID AS
$$
BEGIN

    EXECUTE FORMAT('CREATE TABLE tag_mappings_%1$s_reordered PARTITION OF tag_mappings_reordered FOR VALUES IN (%1$s)', p_tag_domain_id);

    INSERT INTO tag_mappings_reordered (record_id, tag_id, ideal_tag_id, domain_id) SELECT record_id, tag_id, ideal_tag_id, domain_id FROM tag_mappings WHERE domain_id = p_tag_domain_id;

    EXECUTE FORMAT('DROP TABLE tag_mappings_%1s', p_tag_domain_id);

    EXECUTE FORMAT('ALTER TABLE tag_mappings_%1$s_reordered RENAME TO tag_mappings_%1$s', p_tag_domain_id);
END;
$$ LANGUAGE plpgsql;

CREATE TABLE tag_mappings_reordered
(
    record_id    INTEGER  NOT NULL REFERENCES records (record_id),
    tag_id       INTEGER  NOT NULL REFERENCES tags (tag_id),
    ideal_tag_id INTEGER  NULL REFERENCES tags (tag_id) DEFAULT NULL,
    domain_id    SMALLINT NOT NULL REFERENCES tag_domains (tag_domain_id),
    PRIMARY KEY (domain_id, record_id, tag_id)
) PARTITION BY LIST (domain_id);

DO
$$
    DECLARE
        r RECORD;
    BEGIN
        FOR r IN SELECT tag_domain_id FROM tag_domains ORDER BY tag_domain_id
            LOOP
                PERFORM tag_mappings_reorder_v1(r.tag_domain_id);
            END LOOP;
    END;
$$;

DROP TABLE tag_mappings;
ALTER TABLE tag_mappings_reordered
    RENAME TO tag_mappings;

DROP FUNCTION IF EXISTS tag_mappings_reorder_v1;

CREATE TRIGGER tag_virtuals_after_mappings
    AFTER INSERT OR UPDATE OR DELETE
    ON tag_mappings
    FOR EACH ROW
EXECUTE FUNCTION tag_virtuals_mappings_trigger();