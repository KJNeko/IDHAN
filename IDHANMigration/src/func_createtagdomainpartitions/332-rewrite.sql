CREATE OR REPLACE FUNCTION createtagdomainpartitions()
    RETURNS TRIGGER AS
$$
BEGIN
    EXECUTE 'CREATE TABLE IF NOT EXISTS tag_mappings_domain_' || new.tag_domain_id ||
            ' PARTITION OF tag_mappings FOR VALUES IN (' ||
            new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE IF NOT EXISTS tag_aliases_domain_' || new.tag_domain_id ||
            ' PARTITION OF tag_aliases FOR VALUES IN (' ||
            new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE IF NOT EXISTS tag_parents_domain_' || new.tag_domain_id ||
            ' PARTITION OF tag_parents FOR VALUES IN (' ||
            new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE IF NOT EXISTS active_tag_mappings_domain_' || new.tag_domain_id ||
            ' PARTITION OF active_tag_mappings FOR VALUES IN (' ||
            new.tag_domain_id || ')';
    EXECUTE 'CREATE TABLE IF NOT EXISTS active_tag_mappings_parents_domain_' || new.tag_domain_id ||
            ' PARTITION OF active_tag_mappings_parents FOR VALUES IN (' ||
            new.tag_domain_id || ')';

    RETURN new;
END;
$$ LANGUAGE plpgsql;
