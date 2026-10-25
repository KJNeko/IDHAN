DROP TRIGGER accumulate_tag_count_trigger ON active_tag_mappings;
DROP TRIGGER accumulate_tag_count_update_trigger ON active_tag_mappings;

CREATE TRIGGER collect_tag_count_storage_insert
    AFTER INSERT
    ON active_tag_mappings
    REFERENCING NEW TABLE AS new_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_storage();

CREATE TRIGGER collect_tag_count_storage_delete
    AFTER DELETE
    ON active_tag_mappings
    REFERENCING OLD TABLE AS old_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_storage();

CREATE TRIGGER collect_tag_count_storage_update
    AFTER UPDATE
    ON active_tag_mappings
    REFERENCING OLD TABLE AS old_mappings NEW TABLE AS new_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_storage();
