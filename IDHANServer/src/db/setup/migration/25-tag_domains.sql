CREATE OR REPLACE FUNCTION createTagMappingsDomain() RETURNS TRIGGER
    language plpgsql
AS
$$
DECLARE
    domain_id INTEGER;
BEGIN
    FOR domain_id IN SELECT tag_domain_id FROM tag_domains
        LOOP
            IF EXISTS (SELECT 1 FROM information_schema.tables WHERE table_name = FORMAT('tag_mappings_%s', domain_id)) THEN
                CONTINUE;
            END IF;

            -- mappings
            EXECUTE FORMAT(
                    'CREATE TABLE IF NOT EXISTS tag_mappings_%s PARTITION OF tag_mappings FOR VALUES IN (%s)',
                    domain_id, domain_id);
            -- aliases
            EXECUTE FORMAT(
                    'CREATE TABLE IF NOT EXISTS tag_aliases_%s PARTITION OF tag_aliases FOR VALUES IN (%s)',
                    domain_id, domain_id);
            -- siblings
            EXECUTE FORMAT(
                    'CREATE TABLE IF NOT EXISTS tag_siblings_%s PARTITION OF tag_siblings FOR VALUES IN (%s)',
                    domain_id, domain_id);
            -- parents
            EXECUTE FORMAT(
                    'CREATE TABLE IF NOT EXISTS tag_parents_%s PARTITION OF tag_parents FOR VALUES IN (%s)',
                    domain_id, domain_id);
        end loop;
    RETURN NEW;
END;
$$;

-- CREATE OR REPLACE FUNCTION destroyTagMappingsDomain() RETURNS TRIGGER
--     language plpgsql
-- AS
-- $$
-- DECLARE
--     domain_id INTEGER;
-- BEGIN
--     FOR domain_id IN SELECT tag_domain_id FROM tag_domains
--         LOOP
--             -- mappings
--             EXECUTE FORMAT(
--                     'DROP TABLE IF EXISTS tag_mappings_%s',
--                     domain_id);
--             -- aliases
--             EXECUTE FORMAT(
--                     'DROP TABLE IF EXISTS tag_aliases_%s',
--                     domain_id);
--             -- siblings
--             EXECUTE FORMAT(
--                     'DROP TABLE IF EXISTS tag_siblings_%s',
--                     domain_id);
--             -- parents
--             EXECUTE FORMAT(
--                     'DROP TABLE IF EXISTS tag_parents_%s',
--                     domain_id);
--         end loop;
--
--     RETURN NEW;
-- END;
-- $$;

CREATE TRIGGER tag_domain_insert
    AFTER INSERT
    ON tag_domains
    REFERENCING NEW TABLE AS inserted
    FOR EACH STATEMENT
EXECUTE FUNCTION createTagMappingsDomain();

-- CREATE TRIGGER tag_domain_delete
--     AFTER DELETE
--     ON tag_domains
--     REFERENCING OLD TABLE AS deleted
--     FOR EACH STATEMENT
-- EXECUTE FUNCTION destroyTagMappingsDomain();

INSERT INTO tag_domains(domain_name)
VALUES ('default');