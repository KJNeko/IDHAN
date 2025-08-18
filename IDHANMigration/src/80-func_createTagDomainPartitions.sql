CREATE OR REPLACE FUNCTION createtagdomainpartitions()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE 'CREATE TABLE tag_mappings_domain_' || new.tag_domain_id || ' PARTITION OF tag_mappings FOR VALUES IN (' || new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE tag_aliases_domain_' || new.tag_domain_id || ' PARTITION OF tag_aliases FOR VALUES IN (' || new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE tag_parents_domain_' || new.tag_domain_id || ' PARTITION OF tag_parents FOR VALUES IN (' || new.tag_domain_id || ')';

    RETURN new;
END;
$$ LANGUAGE plpgsql;

-- Create trigger to execute the function before insert
CREATE TRIGGER trg_create_tag_domain_partitions
    BEFORE INSERT
    ON tag_domains
    FOR EACH ROW
EXECUTE FUNCTION createtagdomainpartitions();

INSERT INTO tag_domains (domain_name)
VALUES ('default');