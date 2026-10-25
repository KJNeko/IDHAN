DROP TRIGGER trg_accumulate_tag_count_parents ON active_tag_mappings_parents;

CREATE TRIGGER collect_tag_count_parents_insert
    AFTER INSERT
    ON active_tag_mappings_parents
    REFERENCING NEW TABLE AS new_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_parents();

CREATE TRIGGER collect_tag_count_parents_delete
    AFTER DELETE
    ON active_tag_mappings_parents
    REFERENCING OLD TABLE AS old_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_parents();

CREATE TRIGGER collect_tag_count_parents_update
    AFTER UPDATE
    ON active_tag_mappings_parents
    REFERENCING OLD TABLE AS old_mappings NEW TABLE AS new_mappings
    FOR EACH STATEMENT
EXECUTE FUNCTION collect_tag_count_parents();
