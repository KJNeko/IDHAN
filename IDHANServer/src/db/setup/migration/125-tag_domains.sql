CREATE OR REPLACE FUNCTION createtagmappingsdomain() RETURNS TRIGGER
    LANGUAGE plpgsql
AS
$$
BEGIN

    -- mappings
    EXECUTE FORMAT(
            'CREATE TABLE tag_mappings_%1$s PARTITION OF tag_mappings FOR VALUES IN (%1$s)',
            new.tag_domain_id);

    -- aliases
    EXECUTE FORMAT(
            'CREATE TABLE tag_aliases_%1$s PARTITION OF tag_aliases FOR VALUES IN (%1$s)',
            new.tag_domain_id);

    -- siblings
    EXECUTE FORMAT(
            'CREATE TABLE tag_siblings_%1$s PARTITION OF tag_siblings FOR VALUES IN (%1$s)',
            new.tag_domain_id);

    -- parents
    EXECUTE FORMAT(
            'CREATE TABLE tag_parents_%1$s PARTITION OF tag_parents FOR VALUES IN (%1$s)',
            new.tag_domain_id);

    RETURN new;

    -- Lock will be automatically released at transaction end
    -- PERFORM pg_advisory_unlock(hashtext('information_schema_lock'));
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
    FOR EACH ROW
EXECUTE FUNCTION createtagmappingsdomain();

-- CREATE TRIGGER tag_domain_delete
--     AFTER DELETE
--     ON tag_domains
--     REFERENCING OLD TABLE AS deleted
--     FOR EACH STATEMENT
-- EXECUTE FUNCTION destroyTagMappingsDomain();

INSERT INTO tag_domains(domain_name)
VALUES ('default');