-- Statement triggers are not cloned to partitions. Install them on leaf tables too
-- so direct partition writes have the same counting contract as writes to the root.
CREATE FUNCTION install_tag_count_collectors(target REGCLASS, collector REGPROC) RETURNS VOID AS
$$
BEGIN
    EXECUTE format('CREATE OR REPLACE TRIGGER collect_tag_count_insert AFTER INSERT ON %s ' ||
                   'REFERENCING NEW TABLE AS new_mappings FOR EACH STATEMENT EXECUTE FUNCTION %s()', target, collector);
    EXECUTE format('CREATE OR REPLACE TRIGGER collect_tag_count_delete AFTER DELETE ON %s ' ||
                   'REFERENCING OLD TABLE AS old_mappings FOR EACH STATEMENT EXECUTE FUNCTION %s()', target, collector);
    EXECUTE format('CREATE OR REPLACE TRIGGER collect_tag_count_update AFTER UPDATE ON %s ' ||
                   'REFERENCING OLD TABLE AS old_mappings NEW TABLE AS new_mappings ' ||
                   'FOR EACH STATEMENT EXECUTE FUNCTION %s()', target, collector);
END;
$$ LANGUAGE plpgsql;
